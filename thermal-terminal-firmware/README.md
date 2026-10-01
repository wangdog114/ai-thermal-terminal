# Thermal Terminal Firmware

ESP32-S3 热敏聊天终端固件，目标硬件为 `ESP32-S3-DevKitC-1-N16R8`。工程使用 ESP-IDF 和 C++17。

## 当前范围

已经建立：

- 16MB Flash、双 OTA、资源分区和 coredump 分区。
- 应用状态、跨任务事件和持久化设置数据结构。
- 显示、输入、打印和 Worker 客户端接口边界。
- 可分块输入的 TPB1 解析器、PackBits 解压和 CRC32 校验。
- 不依赖 ESP-IDF 的桌面协议测试。

SSD1306、1838B 和 CSN-A2 驱动已接入；屏幕 UI 已有主页、输入、历史、设置和点阵预览。编辑器支持英文多击、词典 T9 输入和覆盖 GB2312 的中文笔画输入，OLED 中文使用 16×16 Unifont 字形；界面语言可在设置页切换为中文。

## 板级配置

引脚、SSD1306 的 7 位 I²C 地址、打印机 UART 参数、NEC 遥控键和长按重发阈值统一放在 [`components/board_config/include/board_config/board_config.hpp`](components/board_config/include/board_config/board_config.hpp)。`kRetryLongPressMs` 默认 `800`，用于过滤 NEC 提前到达的重复帧；修改该文件后运行 `idf.py -B build-idf build` 并重新刷写，不需要改 `sdkconfig` 或进入 menuconfig。遥控键表可同时修改 `command` 和 `LogicalKey`，同一个命令码不可重复；`ir_map` 可检查刷入后的映射。Wi-Fi 凭据、Worker Token、会话及 UI 用户设置仍保存在 NVS，不属于板级配置。

打印机换成 TTL 版本并确认输出电平与 ESP32 GPIO 兼容之后，再把 `kPrinterInterfaceReady` 改为 `true`。默认值 `false` 会阻止所有 UART 打印命令，避免误接到 RS232 设备。

## 构建环境

当前已用 ESP-IDF 6.1 验证：

```sh
source /home/wangdog/.espressif/v6.1/esp-idf/export.sh
idf.py -B build-idf set-target esp32s3
idf.py -B build-idf build
```

首次构建会通过 Component Manager 下载并锁定 `espressif/cjson`，版本记录在 `dependencies.lock`。

刷写并查看串口日志：

```sh
idf.py -B build-idf -p /dev/ttyACM0 flash monitor
```

当前开发固件在 USB Serial/JTAG 上提供配置命令：

```text
status
set_wifi <ssid> <password>
set_worker <worker_url> <terminal_token>
models
ask <message>
history
oled_test
i2c_scan
show_last
preview [x y]
ir_scan
ir_map
printer_test
printer_text
printer_selftest
printer_status
printer_baud <baudrate>
print_last
reboot
```

`status` 只显示 Token 是否已设置，不输出 Token 内容，并显示当前生效的 Worker URL。Wi-Fi 和 Worker 配置写入 NVS，重启后保留。Wi-Fi 可直接从 OLED 设置页扫描和配置；Worker Token 仍需通过 `set_worker` 命令输入，串口终端可能回显 Token，请在私有终端操作。

当前固件把 CSN-A2 的串口帧设为 8N1、9600 baud，打印数据使用手册 8.2.4 的 `DC2 V nL nH [48 字节/行]` 指令。手册没有在该章节固定波特率，因此如果你的版本由拨码开关设成其他速率，可执行 `printer_baud 19200`、`printer_status`，再依次尝试实际设定值。`printer_status` 按手册 8.2.6 发送 `ESC v 0`，必须连接打印机 TXD 到 ESP32 RX 才能收到 1 字节。`printer_text` 打印纯 ASCII 诊断行，`printer_selftest` 请求打印机内部测试页；只有这些命令正常后再运行 `printer_test` 或 `print_last`。

注意：CSN-A2 有 TTL 和 RS232 两种硬件版本。当前计划退换成 TTL 版本：ESP32 TX 接打印机 RXD，打印机 TXD 接 ESP32 RX，双方共地，打印机 VH 按手册单独供电。必须先确认替换件的 TXD 电平不会超过 ESP32 GPIO 的 3.3V 容限；不要把 RS232 或 5V TXD 直接接到 ESP32 RX。若之前曾直连 RS232，先检查 ESP32 RX GPIO 是否仍正常。

旧版固件曾允许直接执行 `printer_status` 做 GPIO17/18 回环检查；新版固件在安全开关关闭时会阻止该命令，避免误向 RS232 接口发送信号。

联网测试时先执行 `status`，确认 `wifi_connected=yes`，再用 `models` 读取服务端模型目录。`ask` 会通过 Worker 生成回复，并把 TPB1 点阵流式解压、校验后暂存到 PSRAM；成功后打印尺寸和消息 ID，但不会自动打印。`history` 读取当前会话最新一页 D1 消息。Worker 的 Cloudflare 证书链使用交叉签名，因此固件启用了 ESP-IDF 的交叉签名证书链验证，继续校验 HTTPS 证书。

SSD1306 当前优先使用 7 位 I²C 地址 `0x3D`，无应答时自动尝试 `0x3C`；当前板级配置为 SDA=GPIO1、SCL=GPIO2。屏幕标注的 8 位读地址 `0x7B` 对应 7 位 `0x3D`，写地址为 `0x7A`。1838B 的 OUT 当前接 GPIO15。两者与 ESP32 共地，信号上拉到 3.3V。若接线不同，修改上述板级配置文件后重新编译刷写。

启动时 OLED 显示主页，1838B 红外接收器常驻读取遥控器。`CH-`/`CH+` 选择，`CH` 确认，`EQ` 返回主页。主页可进入英文多击输入、词典 T9 输入、中文笔画输入、历史、设置和回复预览；输入页中 `100+` 依次切换 `UPPER`、`lower`、`123`、`STROKE`、`WORD`。`WORD` 模式下使用 `2` 至 `9` 输入 T9 数字串，底部整行显示当前候选词及序号，超过显示宽度时自动滚动；`CH-`/`CH+` 选择候选、`PREV`/`NEXT` 翻页、`CH` 插入原始大小写词形。`200+` 打开符号选择条；再次按 `200+` 或按 `EQ` 可退出面板，选择后按 `CH` 插入。英文模式使用半角符号，笔画模式使用全角符号。笔画模式下 `1` 至 `5` 分别为横、竖、撇、点、折，候选用方向键选择、`CH` 插入。候选先列出笔画完全匹配的汉字，再列前缀匹配项；同组按 `gb2312_by_freq.txt` 中的字频从高到低排列。数字键多击输入字符，`VOL-` 退格（按住可连续删除），`VOL+` 插入空格，`PREV`/`NEXT` 左右移动光标，短按 `PLAY/PAUSE` 发送，长按 `PLAY/PAUSE` 重发会话中的最后一条用户消息。混排编辑统一使用 16px 行高，ASCII 字符占 8px 宽、汉字占 16px 宽；紧凑的纯英文界面仍使用小字。中文编辑按 16×16 字形显示最多两行草稿与一行候选。光标闪烁且可在文本中间插入/删除。当前编辑缓冲区上限由板级配置文件中的 `kMaxDraftLength` 控制，默认 2048 字节。历史页方向键选择，确认渲染机器人回复或查看用户消息，右键读取更早记录。预览页用 `2`/`8`/`4`/`6` 分别向上/下/左/右平移 384px 点阵；离开编辑页后 `200+` 恢复清空历史，`100+` 恢复打印，但在 TTL 替换件尚未确认并启用安全开关之前会被阻止。

设置页可修改模型、推理等级、上下文、自动打印、字号、行距、底部走纸、界面语言和 HTTPS 开关，修改后写入 NVS。HTTPS 默认开启；关闭后 Worker 请求使用 HTTP，Token 和消息将明文传输，仅适用于可信的本地服务。`set_worker` 传入带 `http://` 或 `https://` 的地址时会同步更新开关；之后可在 OLED 设置页切换协议，Worker 的主机、端口和路径保持不变。长模型名称会在选中时滚动显示；英文界面的推理等级使用 `MINIMUM`、`LOW`、`MEDIUM`、`HIGH` 等本地标签，并在切换模型时尽量保留当前等级。设置页的 Wi-Fi 项可扫描附近接入点；方向键选择，`CH` 连接开放网络或进入密码输入。加密网络密码通过多击输入，`100+` 切换大小写/数字，`200+` 选择半角符号，`VOL-` 退格，`VOL+` 输入空格，`CH` 提交，`EQ` 返回。密码只显示为星号；连接取得 IP 后才保存到 NVS，失败会恢复原网络配置。Worker URL 和 Token 仍使用串口 `set_worker` 配置。`oled_test` 可重画测试图；`show_last` 从 D1 重渲染最后一条机器人回复；`preview 128 0` 用串口指定 OLED 点阵窗口。执行 `ir_scan` 时 UI 暂停读取红外 10 秒。

打印机的板级安全开关 `kPrinterInterfaceReady` 默认关闭；确认替换件为兼容 ESP32 的 TTL 接口后，在板级配置文件中开启并重新刷写。

若 OLED 没有显示，运行 `i2c_scan` 查看 GPIO8/9 总线上实际应答的 7 位地址，再检查屏幕供电、共地和 SDA/SCL 接线。

如果设备节点不是 `/dev/ttyACM0`，替换为系统实际识别的 USB 串口。没有开发板连接时，可以先运行协议核心测试：

```sh
cmake -S test/host -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

## 模块

```text
main                 固件入口，后续创建 FreeRTOS 任务
components/app_core  应用状态与事件
components/settings  用户设置及默认值
components/tpb1      TPB1、PackBits、CRC32
components/bitmap_store 仅在整图校验成功后开放的 PSRAM 点阵
components/interfaces 显示、输入、打印、Worker API 抽象
components/terminal_client Worker HTTPS、cJSON 请求和响应解析
components/wifi_manager Wi-Fi STA 连接和重连事件
components/oled_display SSD1306 I²C 驱动和 384px 点阵窗口
components/terminal_ui 128x64 页面、红外导航、英文多击、词典 T9 和中文笔画输入
components/english_dictionary 由 `en_wordlist.combined` 生成的紧凑 T9 词典和 SPIFFS 查询器
components/nec_input 1838B RMT 接收与 NEC 解码
components/board_config 统一板级硬件与遥控器配置
components/csn_a2_printer CSN-A2 UART 输出（手册中的 DC2 V 位图指令）
```

英文词典构建

`en_wordlist.combined` 不会直接编译进应用。构建前由以下脚本提取词形、保留大小写和频率，并生成 `assets/en.t9`：

```sh
python3 components/english_dictionary/tools/build_t9_dictionary.py \
  en_wordlist.combined assets/en.t9
```

`assets/en.t9` 通过现有 `assets` SPIFFS 分区随固件一起刷写，约 2.8MiB。启动时词典会整体载入 PSRAM，查询只在内存中扫描并使用 80KiB 分组索引，避免逐字符访问 SPIFFS 造成延迟。若更新词典，重新生成该文件并执行完整 `idf.py build`/`flash`。

后续任务：

```text
ui_task       已实现基础页面、遥控导航和点阵窗口
input_task    编辑器输入状态机已集成在 terminal_ui
network_task  Wi-Fi、Worker API 和 TPB1 接收
printer_task  打印队列、流控和走纸
```

当前 UI 任务直接调用已验证的 Worker 客户端，等待网络请求时显示状态页；网络任务队列化和 `AppEvent` 分发仍是后续工作。TPB1 完整校验后才开放点阵预览；默认不会自动打印。

## 设置

`UserSettings` 的默认值与服务端一致：

```text
use_context   true
auto_print    false
font_size     22
line_height   1.45
margin        12
bottom_feed   24
threshold     185
dither        threshold
compression   packbits
band_height   256
```

普通设置、Wi-Fi 凭据和会话 ID 使用不同的 NVS namespace；NEC 键位映射由板级配置文件编译进固件。Token 不应写入源码仓库。
