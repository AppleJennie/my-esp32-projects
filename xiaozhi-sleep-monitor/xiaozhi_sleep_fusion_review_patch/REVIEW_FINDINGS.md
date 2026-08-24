# xiaozhi_sleep_fusion 代码审查结论

## P0 必改

1. R60 query task 未启动
- 现象：rx_bytes 少量增长，valid 不持续增加；心率/呼吸时好时坏。
- 根因：fusion_task_start() 只调用 r60abd1_uart_start_task()，当前 r60abd1_uart_start_task() 只创建 r60abd1_uart_task，没有创建 r60abd1_query_task。
- 修复：见 notes/r60_query_task_patch.txt。

2. SNORE_RX 最新数据无锁读写
- s_latest/s_last_update_ms 在 UART RX task 写，在 fusion_task 读，有数据竞争。
- 修复：drop_in/snore_report_rx.c。

3. MAIN_TX 直接读 sleep_radar_data_get() 返回的裸指针
- 无锁读取雷达结构，会与 R60 RX task 更新并发。
- 修复：drop_in/main_report_uart.c 使用 sleep_radar_data_get_snapshot()。

4. fusion_task.cc 中 fusion_result_t fr 可能未初始化就用于日志
- 修复：见 notes/fusion_task_patch.txt。

5. GPIO19/20 风险
- ESP32-S3 GPIO19/20 是 USB D-/D+。若小智主板使用 native USB/USB-OTG/USB-JTAG，双板 UART 不应使用 19/20。
- 当前日志显示 console 是 GPIO43/44，若只用 CH340 串口且 USB-OTG 不启用，可以临时用；长期建议改成独立空闲 GPIO 并做板级宏。

## P1 建议

1. R60 ring buffer overflow 现在会静默覆盖旧数据，overflow_count 不增加。
2. r60abd1_uart_init() 建议按 param_config -> set_pin -> driver_install 顺序，并处理重复初始化。
3. sleep_health_fusion_get() 返回静态指针，若未来 MCP 直接访问，需要加快照接口。
4. 环境传感器旧 i2c.h 文件必须继续禁止编译，后续统一改 driver/i2c_master.h。
