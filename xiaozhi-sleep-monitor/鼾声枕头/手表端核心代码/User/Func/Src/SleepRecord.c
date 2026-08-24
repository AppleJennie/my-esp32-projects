#include "SleepRecord.h"
#include "DataSave.h"
#include "string.h"

/* ======================= Internal functions ======================= */

static uint8_t calc_checksum(const uint8_t *data, uint8_t len)
{
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

static void summary_to_entry(const SleepSummary_t *sum, SleepRecord_Entry_t *entry)
{
    memset(entry, 0, sizeof(SleepRecord_Entry_t));
    entry->month = sum->month;
    entry->day = sum->day;
    entry->total_min = sum->total_min;
    entry->deep_min = sum->deep_min;
    entry->light_min = sum->light_min;
    entry->rem_min = sum->rem_min;
    entry->awake_min = sum->awake_min;
    entry->avg_hr = sum->avg_hr;
    entry->min_hr = sum->min_hr;
    entry->sleep_score = sum->sleep_score;
    entry->posture_chg = sum->posture_changes;
    if (sum->fall_asleep_min > 255) entry->fall_asleep_min = 255;
    else entry->fall_asleep_min = sum->fall_asleep_min;
    entry->reserved = sum->breath_rate;
    entry->checksum = calc_checksum((const uint8_t *)entry, SLEEP_RECORD_SIZE - 1);
}

static void entry_to_summary(const SleepRecord_Entry_t *entry, SleepSummary_t *sum)
{
    memset(sum, 0, sizeof(SleepSummary_t));
    sum->month = entry->month;
    sum->day = entry->day;
    sum->total_min = entry->total_min;
    sum->deep_min = entry->deep_min;
    sum->light_min = entry->light_min;
    sum->rem_min = entry->rem_min;
    sum->awake_min = entry->awake_min;
    sum->avg_hr = entry->avg_hr;
    sum->min_hr = entry->min_hr;
    sum->sleep_score = entry->sleep_score;
    sum->posture_changes = entry->posture_chg;
    sum->fall_asleep_min = entry->fall_asleep_min;
    sum->breath_rate = entry->reserved;
}

static uint8_t get_head_index(void)
{
    uint8_t head = 0xFF;
    SettingGet(&head, SLEEP_RECORD_ADDR_HEAD, 1);
    if (head >= SLEEP_RECORD_MAX_COUNT) head = 0;
    return head;
}

static uint8_t get_record_count(void)
{
    uint8_t cnt = 0;
    SettingGet(&cnt, SLEEP_RECORD_ADDR_COUNT, 1);
    if (cnt > SLEEP_RECORD_MAX_COUNT) cnt = 0;
    return cnt;
}

static void set_head_index(uint8_t head)
{
    if (head >= SLEEP_RECORD_MAX_COUNT) head = 0;
    SettingSave(&head, SLEEP_RECORD_ADDR_HEAD, 1);
}

static void set_record_count(uint8_t cnt)
{
    if (cnt > SLEEP_RECORD_MAX_COUNT) cnt = SLEEP_RECORD_MAX_COUNT;
    SettingSave(&cnt, SLEEP_RECORD_ADDR_COUNT, 1);
}

/* ======================= Public API ======================= */

void SleepRecord_Init(void)
{
    /* Ensure head and count are valid */
    uint8_t head = get_head_index();
    uint8_t cnt = get_record_count();
    if (head == 0xFF || head >= SLEEP_RECORD_MAX_COUNT) {
        head = 0;
        set_head_index(head);
    }
    if (cnt > SLEEP_RECORD_MAX_COUNT) {
        cnt = 0;
        set_record_count(cnt);
    }
}

bool SleepRecord_Save(const SleepSummary_t *summary)
{
    if (summary == NULL) return false;

    SleepRecord_Entry_t entry;
    summary_to_entry(summary, &entry);

    uint8_t head = get_head_index();
    uint8_t cnt = get_record_count();

    /* Write to current head position */
    uint8_t addr = SLEEP_RECORD_ADDR_BASE + head * SLEEP_RECORD_SIZE;
    SettingSave((uint8_t *)&entry, addr, SLEEP_RECORD_SIZE);

    /* Advance head */
    head = (head + 1) % SLEEP_RECORD_MAX_COUNT;
    set_head_index(head);

    if (cnt < SLEEP_RECORD_MAX_COUNT) {
        cnt++;
        set_record_count(cnt);
    }

    return true;
}

bool SleepRecord_LoadLatest(SleepSummary_t *summary)
{
    return SleepRecord_LoadByIndex(0, summary);
}

bool SleepRecord_LoadByIndex(uint8_t idx, SleepSummary_t *summary)
{
    if (summary == NULL) return false;

    uint8_t cnt = get_record_count();
    if (idx >= cnt) return false;

    uint8_t head = get_head_index();
    /* head points to next write position, so latest is (head - 1) */
    uint8_t pos = (head + SLEEP_RECORD_MAX_COUNT - 1 - idx) % SLEEP_RECORD_MAX_COUNT;

    uint8_t addr = SLEEP_RECORD_ADDR_BASE + pos * SLEEP_RECORD_SIZE;
    SleepRecord_Entry_t entry;
    memset(&entry, 0, sizeof(entry));
    SettingGet((uint8_t *)&entry, addr, SLEEP_RECORD_SIZE);

    /* Verify checksum */
    uint8_t cs = calc_checksum((const uint8_t *)&entry, SLEEP_RECORD_SIZE - 1);
    if (cs != entry.checksum) return false;

    entry_to_summary(&entry, summary);
    return true;
}

uint8_t SleepRecord_GetCount(void)
{
    return get_record_count();
}

void SleepRecord_Clear(void)
{
    uint8_t zero = 0;
    set_head_index(0);
    set_record_count(0);
    /* Optionally erase the record area */
    for (uint8_t i = 0; i < SLEEP_RECORD_MAX_COUNT; i++) {
        uint8_t addr = SLEEP_RECORD_ADDR_BASE + i * SLEEP_RECORD_SIZE;
        uint8_t buf[SLEEP_RECORD_SIZE];
        memset(buf, 0, SLEEP_RECORD_SIZE);
        SettingSave(buf, addr, SLEEP_RECORD_SIZE);
    }
}
