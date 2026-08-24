# OV-Watch Core Source Code

Extracted minimal core code from [OV-Watch](https://github.com/No-Chicken/OV-Watch) — an open-source smartwatch based on STM32F411CEU6 + LVGL v8.2 + FreeRTOS.

## Architecture

```
core_code/
├── User/                    # Application layer (核心应用层)
│   ├── GUI_App/             # LVGL GUI framework & screens
│   │   ├── ui.c/h           # UI init & main framework
│   │   ├── ui_helpers.c/h   # Helper functions (animations, styles)
│   │   └── Screens/         # ~20 screen pages
│   ├── Func/                # Business logic
│   │   ├── PageManager.c/h  # Page stack navigation (back/load)
│   │   ├── pubsub.c/h       # Pub/Sub message bus
│   │   ├── HWDataAccess.c/h # Hardware data access layer
│   │   ├── SleepAlgorithm.c/h # Sleep detection algorithm
│   │   ├── SleepRecord.c/h  # Sleep data recording
│   │   └── StrCalculate.c/h # String/number formatting
│   └── Tasks/               # FreeRTOS tasks
│       ├── user_TasksInit.c   # Task creation
│       ├── user_KeyTask.c     # Button input handling
│       ├── user_ScrRenewTask.c # Screen refresh tick
│       ├── user_SensUpdateTask.c # Sensor data polling
│       ├── user_RunModeTasks.c   # Watch mode switching
│       ├── user_SleepMonitorTask.c # Sleep monitoring
│       ├── user_ChargCheckTask.c   # Charge detection
│       ├── user_DataSaveTask.c     # EEPROM data storage
│       └── user_MessageSendTask.c  # Bluetooth messaging
│
├── BSP/                     # Board Support Package (驱动层)
│   ├── LCD/                 # ST7789 display driver
│   ├── TOUCH/               # CST816 touch driver
│   ├── EM7028/              # Heart rate / SpO2 sensor
│   ├── MPU6050/             # 6-axis IMU (DMP motion)
│   ├── LSM303DLH/           # 3-axis compass
│   ├── AHT21/               # Temperature & humidity
│   ├── SPL06_001/           # Barometric pressure
│   ├── KT6328/              # Bluetooth module
│   ├── BL24C02/             # EEPROM (data storage)
│   ├── POWER/               # Power management
│   ├── Motor/               # Vibration motor
│   ├── KEY/                 # Physical buttons
│   └── IIC/                 # I2C HAL wrapper
│
├── Core/                    # MCU core (STM32CubeMX generated)
│   ├── Inc/                 # Headers (FreeRTOSConfig, HAL conf, ISRs)
│   └── Src/                 # main.c, freertos.c, ISRs, peripheral init
│
├── SYSTEM/                  # System utilities (delay, sys)
├── LVGL_porting/            # LVGL display/input porting + lv_conf.h
├── lv_sim_user_test/        # PC simulator version of User/ (simplified)
├── gen_font.py              # Font C-code generation script
└── README.md
```

## Key Design Patterns

- **Page Stack Navigation**: `PageManager` maintains a stack (max depth 6) for page push/pop, similar to mobile app navigation
- **Pub/Sub Message Bus**: `pubsub` provides loose coupling between tasks and UI components
- **Task-Based Architecture**: Each function (sensor polling, key scanning, screen refresh, sleep monitor) runs as an independent FreeRTOS task
- **Dual-Target Code**: The `User/` GUI code compiles both for STM32 embedded target and PC simulator (`lv_sim_user_test/`)

## What's NOT Included

- Third-party libraries: LVGL, FreeRTOS, STM32 HAL/CMSIS (needed to build, get from official repos)
- MDK-ARM IDE project files
- BootLoader (IAP_F411) project
- Hardware PCB/gerber/schematic files
- 3D model STL files
- Font/Image auto-generated C arrays (use `gen_font.py` to regenerate)
- Pre-built firmware binaries
