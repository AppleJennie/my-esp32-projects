#include "SleepAlgorithm.h"
#include "math.h"
#include "string.h"
#include "MPU6050.h"

/* ======================= Internal defines ======================= */

/* PPG peak detection: similar to original HR_Calculate but with RR recording */
#define PPG_PEAK_WINDOW   7
#define PPG_MIN_PEAK_MS   425   /* ~141 BPM max */

/* Motion thresholds (scaled accelerometer variance) */
#define MOTION_WAKE_THRESHOLD     80000UL
#define MOTION_REM_THRESHOLD      15000UL
#define MOTION_LIGHT_THRESHOLD    4000UL

/* HR thresholds for sleep staging */
#define HR_WAKE_HIGH_THRESHOLD    75
#define HR_DEEP_LOW_THRESHOLD     55
#define HR_STD_WAKE_THRESHOLD     50   /* x10 */
#define HR_STD_REM_THRESHOLD      80   /* x10 */

/* HRV thresholds (RMSSD in ms) */
#define HRV_DEEP_THRESHOLD        35
#define HRV_REM_THRESHOLD         45

/* Breath rate: PPG baseline drift estimation */
#define BREATH_BUF_LEN            128

/* ======================= Internal types ======================= */

typedef struct {
    uint16_t data[PPG_PEAK_WINDOW];
    uint32_t time[PPG_PEAK_WINDOW];
    uint8_t idx;
    uint8_t count;
} PPG_CircBuf_t;

typedef struct {
    uint16_t rr[RR_QUEUE_LEN];    /* RR intervals in ms */
    uint8_t  head;
    uint8_t  count;
} RR_Queue_t;

typedef struct {
    /* PPG */
    PPG_CircBuf_t ppg_buf;
    uint32_t last_peak_time;
    uint32_t prev_peak_time;
    uint8_t  instant_hr;
    uint8_t  hr_samples;
    uint16_t hr_sum;
    uint16_t hr_min;
    uint16_t hr_max;
    uint32_t hr_sq_sum;           /* For std dev */

    /* RR */
    RR_Queue_t rr_queue;

    /* Accelerometer */
    int32_t acc_sum_x, acc_sum_y, acc_sum_z;
    int32_t acc_sq_sum;           /* Sum of squared magnitudes for variance */
    uint16_t acc_samples;
    uint8_t  posture;
    uint8_t  posture_changes;
    uint8_t  last_posture;
    uint32_t motion_index;
    int32_t  last_acc_mag;        /* For motion index difference */

    /* Breath rate: simple PPG DC trend */
    uint16_t breath_buf[BREATH_BUF_LEN];
    uint8_t  breath_idx;
    uint8_t  breath_count;

    /* Epoch */
    uint16_t epoch_sample_count;
    uint8_t  epoch_started;
    uint32_t epoch_start_tick;

} SleepAlgo_State_t;

/* ======================= Internal variables ======================= */
static SleepAlgo_State_t g_sa;

/* ======================= Internal functions ======================= */

static uint8_t ppg_find_peak(PPG_CircBuf_t *buf, uint8_t *mid_idx_out, uint32_t *mid_time_out)
{
    if (buf->count < PPG_PEAK_WINDOW) return 0;

    /* Reconstruct time-ordered array from circular buffer */
    uint16_t ordered[PPG_PEAK_WINDOW];
    uint32_t ordered_t[PPG_PEAK_WINDOW];
    uint8_t start = (buf->idx + PPG_PEAK_WINDOW - buf->count) % PPG_PEAK_WINDOW;
    for (uint8_t i = 0; i < PPG_PEAK_WINDOW; i++) {
        uint8_t idx = (start + i) % PPG_PEAK_WINDOW;
        ordered[i] = buf->data[idx];
        ordered_t[i] = buf->time[idx];
    }

    uint8_t mid = 3;
    uint16_t mid_val = ordered[mid];

    if ((ordered[3] >= ordered[2]) && (ordered[3] >= ordered[1]) && (ordered[3] > ordered[0])
        && (ordered[3] >= ordered[4]) && (ordered[3] >= ordered[5]) && (ordered[3] > ordered[6])) {
        *mid_idx_out = (start + mid) % PPG_PEAK_WINDOW;
        *mid_time_out = ordered_t[mid];
        return 1;
    }
    return 0;
}

static void rr_enqueue(uint16_t rr_ms)
{
    if (rr_ms < 300 || rr_ms > 2000) return; /* 30-200 BPM filter */
    g_sa.rr_queue.rr[g_sa.rr_queue.head] = rr_ms;
    g_sa.rr_queue.head = (g_sa.rr_queue.head + 1) % RR_QUEUE_LEN;
    if (g_sa.rr_queue.count < RR_QUEUE_LEN) g_sa.rr_queue.count++;
}

static uint16_t calc_rmssd(void)
{
    if (g_sa.rr_queue.count < 4) return 0;
    uint32_t sum_sq_diff = 0;
    uint8_t n = g_sa.rr_queue.count;
    uint8_t idx = (g_sa.rr_queue.head + RR_QUEUE_LEN - n) % RR_QUEUE_LEN;
    for (uint8_t i = 0; i < n - 1; i++) {
        uint8_t a = (idx + i) % RR_QUEUE_LEN;
        uint8_t b = (idx + i + 1) % RR_QUEUE_LEN;
        int32_t diff = (int32_t)g_sa.rr_queue.rr[a] - (int32_t)g_sa.rr_queue.rr[b];
        if (diff < 0) diff = -diff;
        sum_sq_diff += (uint32_t)(diff * diff);
    }
    uint32_t mean_sq = sum_sq_diff / (n - 1);
    uint16_t rmssd = (uint16_t)sqrtf((float)mean_sq);
    return rmssd;
}

static uint16_t calc_hr_std(void)
{
    if (g_sa.hr_samples < 2) return 0;
    float mean = (float)g_sa.hr_sum / g_sa.hr_samples;
    float var = ((float)g_sa.hr_sq_sum / g_sa.hr_samples) - (mean * mean);
    if (var < 0) var = 0;
    return (uint16_t)(sqrtf(var) * 10.0f); /* x10 */
}

static uint8_t calc_breath_rate(void)
{
    if (g_sa.breath_count < BREATH_BUF_LEN) return 0;
    /* Simple zero-crossing on PPG DC trend (stored in breath_buf) */
    uint8_t crossings = 0;
    uint16_t prev = g_sa.breath_buf[0];
    for (uint8_t i = 1; i < BREATH_BUF_LEN; i++) {
        uint16_t curr = g_sa.breath_buf[i];
        /* Use a simple threshold crossing (half of dynamic range) */
        if ((prev < 20000 && curr >= 20000) || (prev >= 20000 && curr < 20000)) {
            crossings++;
        }
        prev = curr;
    }
    /* crossings / 2 = cycles in BREATH_BUF_LEN samples at 25Hz = 5.12 seconds */
    float cycles = crossings / 2.0f;
    float bpm = cycles * (60.0f / 5.12f);
    if (bpm < 6) bpm = 6;
    if (bpm > 30) bpm = 30;
    return (uint8_t)bpm;
}

/* ======================= Public API ======================= */

void SleepAlgo_Init(void)
{
    memset(&g_sa, 0, sizeof(g_sa));
    g_sa.last_peak_time = 0;
    g_sa.prev_peak_time = 0;
    g_sa.last_posture = POSTURE_UNKNOWN;
    g_sa.hr_min = 255;
    g_sa.last_acc_mag = 0;
}

void SleepAlgo_FeedPPG(uint16_t ppg_val, uint32_t tick_ms)
{
    PPG_CircBuf_t *buf = &g_sa.ppg_buf;
    buf->data[buf->idx] = ppg_val;
    buf->time[buf->idx] = tick_ms;
    buf->idx = (buf->idx + 1) % PPG_PEAK_WINDOW;
    if (buf->count < PPG_PEAK_WINDOW) buf->count++;

    /* Breath rate buffer: store PPG value as proxy for baseline */
    g_sa.breath_buf[g_sa.breath_idx] = ppg_val;
    g_sa.breath_idx = (g_sa.breath_idx + 1) % BREATH_BUF_LEN;
    if (g_sa.breath_count < BREATH_BUF_LEN) g_sa.breath_count++;

    /* Peak detection */
    uint8_t mid_idx;
    uint32_t mid_t;
    if (ppg_find_peak(buf, &mid_idx, &mid_t)) {
        uint32_t t = mid_t;
        if (g_sa.last_peak_time != 0) {
            uint32_t interval = t - g_sa.last_peak_time;
            if (interval >= PPG_MIN_PEAK_MS) {
                uint8_t hr = (uint8_t)(60000UL / interval);
                if (hr >= 40 && hr <= 180) {
                    g_sa.instant_hr = hr;
                    if (hr < g_sa.hr_min) g_sa.hr_min = hr;
                    if (hr > g_sa.hr_max) g_sa.hr_max = hr;
                    g_sa.hr_sum += hr;
                    g_sa.hr_sq_sum += (uint16_t)(hr * hr);
                    g_sa.hr_samples++;
                    rr_enqueue((uint16_t)interval);
                }
                g_sa.prev_peak_time = g_sa.last_peak_time;
                g_sa.last_peak_time = t;
            }
        } else {
            g_sa.last_peak_time = t;
        }
    }

    g_sa.epoch_sample_count++;
}

void SleepAlgo_FeedAccel(short ax, short ay, short az)
{
    g_sa.acc_sum_x += ax;
    g_sa.acc_sum_y += ay;
    g_sa.acc_sum_z += az;

    int32_t mag = (int32_t)ax * ax + (int32_t)ay * ay + (int32_t)az * az;
    g_sa.acc_sq_sum += mag;
    g_sa.acc_samples++;

    /* Motion index: accumulate difference from mean */
    int32_t diff = mag - g_sa.last_acc_mag;
    if (diff < 0) diff = -diff;
    g_sa.motion_index += (uint32_t)(diff >> 8); /* scale down */
    g_sa.last_acc_mag = mag;
}

void SleepAlgo_FeedPosture(uint8_t posture)
{
    if (posture != POSTURE_UNKNOWN) {
        g_sa.posture = posture;
        if (g_sa.last_posture != POSTURE_UNKNOWN && g_sa.last_posture != posture) {
            g_sa.posture_changes++;
        }
        g_sa.last_posture = posture;
    }
}

bool SleepAlgo_FinishEpoch(SleepEpoch_t *epoch_out)
{
    if (epoch_out == NULL) return false;
    if (g_sa.epoch_sample_count == 0) return false;

    memset(epoch_out, 0, sizeof(SleepEpoch_t));

    /* HR stats */
    if (g_sa.hr_samples > 0) {
        epoch_out->avg_hr = (uint8_t)(g_sa.hr_sum / g_sa.hr_samples);
        epoch_out->min_hr = (uint8_t)g_sa.hr_min;
        epoch_out->max_hr = (uint8_t)g_sa.hr_max;
        epoch_out->hr_std = calc_hr_std();
    }

    /* HRV */
    epoch_out->hrv_rmssd = calc_rmssd();

    /* Motion */
    epoch_out->motion_index = g_sa.motion_index;

    /* Posture */
    epoch_out->posture = g_sa.posture;
    epoch_out->posture_changes = g_sa.posture_changes;

    /* Breath rate */
    epoch_out->breath_rate = calc_breath_rate();

    /* Validity: if no HR detected, mark invalid */
    if (g_sa.hr_samples < 3) {
        epoch_out->valid = 0;
    } else {
        epoch_out->valid = 1;
    }

    /* Stage classification */
    epoch_out->stage = SleepAlgo_ClassifyEpoch(epoch_out);

    /* Reset epoch accumulators */
    g_sa.hr_sum = 0;
    g_sa.hr_sq_sum = 0;
    g_sa.hr_samples = 0;
    g_sa.hr_min = 255;
    g_sa.hr_max = 0;
    g_sa.acc_sum_x = 0;
    g_sa.acc_sum_y = 0;
    g_sa.acc_sum_z = 0;
    g_sa.acc_sq_sum = 0;
    g_sa.acc_samples = 0;
    g_sa.motion_index = 0;
    g_sa.posture_changes = 0;
    g_sa.epoch_sample_count = 0;

    return true;
}

uint8_t SleepAlgo_GetCurrentHR(void)
{
    return g_sa.instant_hr;
}

uint16_t SleepAlgo_GetCurrentHRV(void)
{
    return calc_rmssd();
}

uint8_t SleepAlgo_GetCurrentPosture(void)
{
    return g_sa.posture;
}

uint8_t SleepAlgo_GetCurrentPostureChanges(void)
{
    return g_sa.posture_changes;
}

uint8_t SleepAlgo_ClassifyEpoch(const SleepEpoch_t *epoch)
{
    if (!epoch->valid) return SLEEP_STAGE_UNKNOWN;

    uint32_t motion = epoch->motion_index;
    uint8_t hr = epoch->avg_hr;
    uint16_t hr_std = epoch->hr_std;
    uint16_t hrv = epoch->hrv_rmssd;
    uint8_t posture_chg = epoch->posture_changes;

    /* Wake: obvious body movement (posture change + high motion) */
    if (posture_chg >= 3 || motion > MOTION_WAKE_THRESHOLD) {
        return SLEEP_STAGE_WAKE;
    }

    /* Wake: HR too high with moderate motion or frequent posture change */
    if ((motion > MOTION_REM_THRESHOLD && hr > HR_WAKE_HIGH_THRESHOLD) ||
        (posture_chg >= 2 && motion > MOTION_LIGHT_THRESHOLD)) {
        return SLEEP_STAGE_WAKE;
    }

    /* Deep sleep: very low motion, low stable HR, elevated HRV, stable posture */
    if (motion < MOTION_LIGHT_THRESHOLD && hr < HR_DEEP_LOW_THRESHOLD &&
        hrv > HRV_DEEP_THRESHOLD && hr_std < HR_STD_WAKE_THRESHOLD && posture_chg == 0) {
        return SLEEP_STAGE_DEEP;
    }

    /* REM: low motion but high HRV and HR variability (like wake but without motion) */
    if (motion < MOTION_REM_THRESHOLD && hrv > HRV_REM_THRESHOLD && hr_std > HR_STD_REM_THRESHOLD && posture_chg <= 1) {
        return SLEEP_STAGE_REM;
    }

    /* REM alternative: low motion, moderate HR, high variability */
    if (motion < MOTION_LIGHT_THRESHOLD && hr_std > HR_STD_REM_THRESHOLD && hrv > HRV_DEEP_THRESHOLD && posture_chg <= 1) {
        return SLEEP_STAGE_REM;
    }

    /* Light sleep: low motion, moderate HR, moderate HRV, may have slight posture change */
    if (motion < MOTION_REM_THRESHOLD && hr >= HR_DEEP_LOW_THRESHOLD && hr <= HR_WAKE_HIGH_THRESHOLD) {
        return SLEEP_STAGE_LIGHT;
    }

    /* Default: if low motion, low HR, stable posture -> deep, else light */
    if (motion < MOTION_LIGHT_THRESHOLD && hr < 60 && posture_chg == 0) {
        return SLEEP_STAGE_DEEP;
    }

    return SLEEP_STAGE_LIGHT;
}

void SleepAlgo_SmoothStages(SleepEpoch_t *epochs, uint16_t count)
{
    if (count < 3) return;
    /* 3-epoch majority vote smoothing */
    for (uint16_t i = 1; i < count - 1; i++) {
        uint8_t s0 = epochs[i - 1].stage;
        uint8_t s1 = epochs[i].stage;
        uint8_t s2 = epochs[i + 1].stage;

        /* If center differs from both neighbors, change it */
        if (s1 != s0 && s1 != s2 && s0 == s2) {
            epochs[i].stage = s0;
        }
    }
}

void SleepAlgo_BuildSummary(const SleepEpoch_t *epochs, uint16_t count, SleepSummary_t *sum)
{
    if (sum == NULL || epochs == NULL || count == 0) return;
    memset(sum, 0, sizeof(SleepSummary_t));

    uint32_t hr_sum = 0;
    uint32_t hrv_sum = 0;
    uint16_t hrv_cnt = 0;
    uint8_t min_hr = 255;
    uint8_t valid_epochs = 0;
    uint16_t awake_epochs = 0;
    uint16_t first_sleep_idx = 0xFFFF;
    uint16_t last_sleep_idx = 0;

    for (uint16_t i = 0; i < count; i++) {
        if (!epochs[i].valid) continue;
        valid_epochs++;

        switch (epochs[i].stage) {
            case SLEEP_STAGE_WAKE:
                sum->awake_min += (SLEEP_EPOCH_SEC / 60);
                awake_epochs++;
                break;
            case SLEEP_STAGE_REM:
                sum->rem_min += (SLEEP_EPOCH_SEC / 60);
                if (first_sleep_idx == 0xFFFF) first_sleep_idx = i;
                last_sleep_idx = i;
                break;
            case SLEEP_STAGE_LIGHT:
                sum->light_min += (SLEEP_EPOCH_SEC / 60);
                if (first_sleep_idx == 0xFFFF) first_sleep_idx = i;
                last_sleep_idx = i;
                break;
            case SLEEP_STAGE_DEEP:
                sum->deep_min += (SLEEP_EPOCH_SEC / 60);
                if (first_sleep_idx == 0xFFFF) first_sleep_idx = i;
                last_sleep_idx = i;
                break;
            default:
                break;
        }

        /* Posture duration statistics per epoch */
        switch (epochs[i].posture) {
            case POSTURE_SUPINE: sum->supine_min  += (SLEEP_EPOCH_SEC / 60); break;
            case POSTURE_LEFT:   sum->left_min    += (SLEEP_EPOCH_SEC / 60); break;
            case POSTURE_RIGHT:  sum->right_min   += (SLEEP_EPOCH_SEC / 60); break;
            case POSTURE_PRONE:  sum->prone_min   += (SLEEP_EPOCH_SEC / 60); break;
            default: break;
        }

        hr_sum += epochs[i].avg_hr;
        if (epochs[i].avg_hr < min_hr) min_hr = epochs[i].avg_hr;
        if (epochs[i].hrv_rmssd > 0) {
            hrv_sum += epochs[i].hrv_rmssd;
            hrv_cnt++;
        }
        sum->posture_changes += epochs[i].posture_changes;
        sum->breath_rate += epochs[i].breath_rate;
    }

    sum->total_min = sum->deep_min + sum->light_min + sum->rem_min;
    if (valid_epochs > 0) {
        sum->avg_hr = (uint8_t)(hr_sum / valid_epochs);
        sum->min_hr = min_hr;
    }
    if (hrv_cnt > 0) {
        sum->avg_hrv = (uint16_t)(hrv_sum / hrv_cnt);
    }
    if (valid_epochs > 0) {
        sum->breath_rate = (uint8_t)(sum->breath_rate / valid_epochs);
    }

    /* Time to fall asleep: first 3 consecutive non-wake epochs */
    if (first_sleep_idx != 0xFFFF && first_sleep_idx < count) {
        sum->fall_asleep_min = (uint8_t)((first_sleep_idx * SLEEP_EPOCH_SEC) / 60);
        if (sum->fall_asleep_min > 120) sum->fall_asleep_min = 120;
    }

    /* Sleep score calculation */
    sum->sleep_score = SleepAlgo_CalculateScore(sum);
}

uint8_t SleepAlgo_CalculateScore(const SleepSummary_t *sum)
{
    if (sum->total_min == 0) return 0;

    /* Base score from duration: 0-40 points */
    uint8_t duration_score = 0;
    if (sum->total_min >= 480) duration_score = 40;          /* 8h */
    else if (sum->total_min >= 420) duration_score = 35;     /* 7h */
    else if (sum->total_min >= 360) duration_score = 30;     /* 6h */
    else if (sum->total_min >= 300) duration_score = 20;     /* 5h */
    else if (sum->total_min >= 240) duration_score = 10;     /* 4h */
    else duration_score = 5;

    /* Deep sleep ratio: 0-30 points (ideal 15-25%) */
    uint8_t deep_ratio = (uint8_t)(((uint32_t)sum->deep_min * 100) / sum->total_min);
    uint8_t deep_score = 0;
    if (deep_ratio >= 15 && deep_ratio <= 25) deep_score = 30;
    else if (deep_ratio >= 10 && deep_ratio <= 30) deep_score = 25;
    else if (deep_ratio >= 5) deep_score = 15;
    else deep_score = 5;

    /* REM ratio: 0-20 points (ideal 20-25%) */
    uint8_t rem_ratio = (uint8_t)(((uint32_t)sum->rem_min * 100) / sum->total_min);
    uint8_t rem_score = 0;
    if (rem_ratio >= 20 && rem_ratio <= 25) rem_score = 20;
    else if (rem_ratio >= 15 && rem_ratio <= 30) rem_score = 15;
    else if (rem_ratio >= 10) rem_score = 10;
    else rem_score = 5;

    /* Awake penalty: -0 to -10 */
    uint8_t awake_penalty = 0;
    if (sum->awake_min > 60) awake_penalty = 10;
    else if (sum->awake_min > 30) awake_penalty = 5;
    else if (sum->awake_min > 15) awake_penalty = 2;

    /* Fall asleep time bonus: 0-10 */
    uint8_t fallasleep_score = 10;
    if (sum->fall_asleep_min > 30) fallasleep_score = 5;
    if (sum->fall_asleep_min > 60) fallasleep_score = 0;

    int16_t score = duration_score + deep_score + rem_score + fallasleep_score - awake_penalty;
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    return (uint8_t)score;
}
