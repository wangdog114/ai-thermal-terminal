# 项目进度

更新时间：2026-10-01。本文记录当前代码和验证状态；部署成功、主机测试通过或固件可编译，不等于打印机、电池供电及完整终端流程已经通过实机验收。总体介绍见 [README.md](README.md)。

## 1. 目标与架构

目标是制作一台尽量低成本、可独立操作的聊天机器人终端：ESP32-S3 通过 Wi-Fi 与 Cloudflare Worker 通信，用 SSD1306 OLED 和 NEC 遥控器完成输入、设置、历史浏览与回复预览，使用 58mm 热敏打印机按需打印。默认**收到回复后不自动打印**。

主链路：`ESP32-S3 -> Cloudflare Worker -> LLM API / D1 -> Vercel Renderer -> Worker -> ESP32-S3`。Worker 负责鉴权、模型选择、会话历史和转发；Vercel 负责较重的 Markdown/公式/表格渲染；结果以 384px 宽的 TPB1 单色点阵返回，供 OLED 横向滚动预览和打印。Worker 层也避免终端直接依赖部分网络环境下难以访问的 Vercel 域名。协议详见 [TPB1 文档](thermal-renderer/docs/tpb1.md)，接口详见 [Worker API](thermal-terminal-worker/docs/api.md)。

## 2. 已完成

| 模块 | 当前实现与关键入口 |
| --- | --- |
| Vercel 渲染 | [渲染 API](thermal-renderer/api/render.js) 提供带 Bearer 鉴权的 `/render` 和 `/health`；[Markdown 解析](thermal-renderer/src/markdown.js) 与 [公式插件](thermal-renderer/src/math-plugin.js) 支持 `$...$`、`$$...$$`、`\(...\)`、`\[...\]`；[renderMarkdownToPng](thermal-renderer/src/render.js)、[pngToMonoBitmap](thermal-renderer/src/raster.js)、[buildTpbDocument](thermal-renderer/src/protocol.js) 完成 384px 单色渲染与 TPB1 编码。 |
| Worker 与 D1 | [路由入口](thermal-terminal-worker/src/index.js)；[handleTerminalRequest](thermal-terminal-worker/src/terminal.js)、`handleTerminalRetry`、`handleTerminalHistory`、`handleTerminalRenderMessage`、`handleTerminalConfig` 处理发送、重发、历史、重渲染和模型目录；[session.js](thermal-terminal-worker/src/db/session.js) 管理 D1 会话/消息及旧数据迁移；[providers](thermal-terminal-worker/src/providers/) 接入 OpenAI、Anthropic、Google。 |
| 固件网络与协议 | [HttpTerminalClient](thermal-terminal-firmware/components/terminal_client/terminal_client.cpp) 默认通过 HTTPS 对接 Worker，OLED 设置页可切换 HTTP/HTTPS；[tpb1::Parser](thermal-terminal-firmware/components/tpb1/parser.cpp) 分块解析并校验长度、CRC、PackBits；[BitmapStore](thermal-terminal-firmware/components/bitmap_store/bitmap_store.cpp) 仅在点阵完整校验后开放 PSRAM 数据。 |
| 固件显示与输入 | [TerminalUi::on_key / render](thermal-terminal-firmware/components/terminal_ui/terminal_ui.cpp) 包含主页、编辑、历史、设置、Wi-Fi 列表/密码、通知和点阵预览状态；[BitmapWindow::draw_utf8_text](thermal-terminal-firmware/components/oled_display/bitmap_window.cpp) 支持混排（ASCII 8×16、汉字 16×16）；[Ssd1306Display](thermal-terminal-firmware/components/oled_display/ssd1306_display.cpp) 与 [NecReceiver / NecDecoder](thermal-terminal-firmware/components/nec_input/) 驱动 OLED 和红外。 |
| 输入法 | 英文大小写多击、数字、中文笔画、英文词典 T9、半角/全角符号和 UTF-8 光标退格已接入 [terminal_ui.cpp](thermal-terminal-firmware/components/terminal_ui/terminal_ui.cpp)。[笔画字库生成器](thermal-terminal-firmware/components/terminal_ui/tools/generate_stroke_data.py) 覆盖 `gb2312_by_freq.txt` 的 6763 字，完整笔画匹配优先、同组按字频排序；[EnglishDictionary::initialize / query](thermal-terminal-firmware/components/english_dictionary/english_dictionary.cpp) 将 [紧凑 T9 文件](thermal-terminal-firmware/assets/en.t9) 载入 PSRAM 查询，并保留英文词形大小写。长词候选整行滚动；`VOL-` NEC 重复帧连续退格不会误触发光标上移。 |
| Wi-Fi 与持久化 | [WifiManager::scan / connect](thermal-terminal-firmware/components/wifi_manager/wifi_manager.cpp) 扫描并连接接入点；[handle_ui_action](thermal-terminal-firmware/main/app_main.cpp) 在获得 IP 后保存新凭据，超时则恢复旧网络；[SettingsStore](thermal-terminal-firmware/components/settings/settings_store.cpp) 将用户、网络、会话设置分开存入 NVS，并保存 HTTPS 开关；旧设备未设置该开关时沿用已存 Worker URL 的协议。 |
| 打印与硬件配置 | [CsnA2Printer](thermal-terminal-firmware/components/csn_a2_printer/csn_a2_printer.cpp) 实现 UART 位图发送、走纸及诊断命令；[board_config.hpp](thermal-terminal-firmware/components/board_config/include/board_config/board_config.hpp) 集中 GPIO、7 位 OLED 地址、遥控器键值、长按阈值和打印安全开关；[partitions.csv](thermal-terminal-firmware/partitions.csv) 定义双 OTA、assets SPIFFS 和 coredump 分区。打印接口当前仍被安全开关禁用。 |

目前通过验证：Renderer 单元测试 **4/4**、Worker 测试 **1/1**、固件主机测试 **2/2**；ESP-IDF 6.1 构建成功，最新应用镜像约 `0x196ff0` 字节，最小应用分区剩余约 **67%**。测试分别见三个子项目的 `test/` 目录。尚未在本轮重新刷写或进行整机测试。

## 3. 重要决策

- **ESP32-S3 N16R8 与分区布局**：16MB Flash、8MB PSRAM 为点阵缓存、词典和双 OTA 留出空间；词典位于独立 SPIFFS 分区，构建/完整刷写由 [CMakeLists.txt](thermal-terminal-firmware/CMakeLists.txt) 处理。
- **Worker + Vercel 分工**：Worker 位于终端前方以改善终端网络可达性；Chromium/Markdown 渲染放在 Vercel，避免占用 Worker 免费层有限 CPU 时间。
- **先保存 Markdown，再渲染点阵**：Worker 将用户与机器人消息写入 D1，渲染失败仍可通过消息 ID 重渲染；上下文只取最近 10 条，控制 LLM 成本。实现位于 [terminal.js](thermal-terminal-worker/src/terminal.js) 和 [session.js](thermal-terminal-worker/src/db/session.js)。
- **384px 单色协议与双用途点阵**：TPB1 的分块、压缩和校验使 ESP32 能流式接收同一份打印/预览数据；OLED 的 128px 窗口可覆盖 384px 宽度。
- **默认手动打印、硬件安全优先**：`auto_print=false`；发现原购买打印机实为 RS232 后，决定退换 TTL 版本，不直接接 GPIO，也不默认启用打印 UART。见 [user_settings.hpp](thermal-terminal-firmware/components/settings/include/settings/user_settings.hpp) 与 [board_config.hpp](thermal-terminal-firmware/components/board_config/include/board_config/board_config.hpp)。
- **设备端输入与词典资源**：英文词典原文约 6.9MiB，预处理成约 2.8MiB 的 T9 文件并在启动时载入 PSRAM，避免每键查 SPIFFS；中文使用 16×16 Unifont 和 GB2312 范围的笔画/字频数据。见 [词典构建脚本](thermal-terminal-firmware/components/english_dictionary/tools/build_t9_dictionary.py) 与 [字库声明](thermal-terminal-firmware/components/terminal_ui/data/NOTICE.md)。

## 4. 未完成 / 下一步

1. **实机回归**：完整刷写固件及 `assets` 分区，验证 OLED 中英混排、笔画/英文词典输入延迟、候选滚动、长按/连续退格、Wi-Fi 选网、Worker 收发和断网恢复；记录实际耗时与内存余量。
2. **TTL 打印机验收**：确认替换件电平、供电、共地及 TX/RX；先跑 `printer_text`、`printer_status`、`printer_test`，再启用 `kPrinterInterfaceReady` 并做真实点阵/长文/缺纸测试。
3. **电池与电源**：完成两节 18650 的保护、充电、稳压及打印峰值电流方案，并验证续航、低电压和复位行为。
4. **终端完全脱离串口配置**：Wi-Fi 已可在 OLED 设置；Worker URL/Token 当前仍需 `set_worker`，需设计安全、可用的设备端配置流程。
5. **异步化与稳定性**：`handle_ui_action` 仍在 `ui_task` 中同步执行 HTTP/扫描等长操作；将网络、重试和打印改为独立任务/队列，补超时、取消、进度和长回复压力测试。现有状态/事件定义见 [app_core](thermal-terminal-firmware/components/app_core/include/app_core/)。
6. **集成与发布**：增加端到端服务端测试、固件实机回归及自动化部署/版本发布流程；继续检查生成字库、第三方字体和资源的许可证要求。

## 5. 已知问题与踩坑

- **打印机接口类型**：原卖家标注 TTL、到货实为 RS232；多波特率测试出现乱码/无回复并非单纯软件问题。RS232 电平不可直连 ESP32，安全开关目前仍关闭。
- **OLED 地址与 RMT 范围**：屏幕标注 `0x7B` 是 8 位读地址，对应 ESP-IDF 使用的 7 位 `0x3D`；1838B 接收曾因 RMT `signal_range_min_ns` 过大报错，现见 [NecReceiver::read](thermal-terminal-firmware/components/nec_input/nec_receiver.cpp)。
- **遥控器重复帧**：`PLAY/PAUSE` 的第一帧重复信号不能直接视为长按；阈值现为可配置的 `kRetryLongPressMs=800`。`VOL-` 映射到逻辑 `kUp`，若先按逻辑方向键处理重复帧会把光标移到开头；现于 [TerminalUi::on_key](thermal-terminal-firmware/components/terminal_ui/terminal_ui.cpp) 优先识别物理命令码。
- **字形/候选布局**：OLED 上 ASCII 与汉字不能都按 16px 宽计算；英文候选词原先四个窄槽无法看全，现改整行显示选中词并滚动。两者均有主机测试，但新 UI 效果仍需实机确认。
- **词典查询延迟**：早期每键从 SPIFFS 随机读取，实机曾达到约 50 秒，改顺序读后仍约 2–5 秒；现将紧凑词典整份载入 PSRAM，尚需重新测实机延迟。预处理器当前跳过原词表中的非 ASCII 词项，见 [build_t9_dictionary.py](thermal-terminal-firmware/components/english_dictionary/tools/build_t9_dictionary.py)。
- **部署与资源**：Vercel 渲染依赖 Chromium、KaTeX 和中文字体，安装阶段会生成未纳入 Git 的资源；`/health` 也需 Bearer Token。Worker 的 [wrangler.toml](thermal-terminal-worker/wrangler.toml) 包含当前 D1 数据库 ID，迁移账号需替换。真实 `.env`、`.dev.vars` 等密钥文件均应保持在 Git 之外。

## 6. 运行、测试、构建

前置：Node.js 22、npm、Cloudflare Wrangler、Vercel CLI、ESP-IDF 6.1；固件主机测试另需 CMake 和 Ninja。真实密钥配置见 [Renderer 示例](thermal-renderer/.env.example)、[Worker 示例](thermal-terminal-worker/.dev.vars.example)；不要提交密钥。

```sh
# 在各子项目目录执行
cd thermal-renderer
npm ci
npm test
npx vercel deploy --prod

cd ../thermal-terminal-worker
npm ci
npm test
npx wrangler d1 migrations apply DB --remote
npx wrangler deploy

cd ../thermal-terminal-firmware
cmake -S test/host -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
source /path/to/esp-idf/export.sh
idf.py -B build-idf build
idf.py -B build-idf -p /dev/ttyACM0 flash monitor
```

部署顺序为 Vercel -> Worker -> 终端。Vercel 的 `RENDER_TOKEN` 必须和 Worker 的 `RENDER_TOKEN` 一致；Worker 的 `RENDER_URL` 指向 Vercel 项目地址。终端使用串口 `set_worker <worker_url> <terminal_token>` 配置服务端，Wi-Fi 可在 OLED 设置页扫描配置。`flash` 必须包含 `assets` SPIFFS 分区中的 `en.t9`，不能只写应用镜像。更多命令和硬件接线见三个子项目的 README。
