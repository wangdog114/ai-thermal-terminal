# AI Thermal Terminal

基于 ESP32-S3 的带热敏打印功能的聊天终端。用户通过 NEC 红外遥控器和 128×64 SSD1306 OLED 完成输入、选模型、浏览历史和预览回复；ESP32 经 Wi-Fi 请求 Cloudflare Worker，Worker 调用 LLM、在 D1 保存会话，再交由 Vercel 将 Markdown 渲染为 384px 宽的单色点阵。点阵以 TPB1 二进制格式返回终端，用于 OLED 横向预览和 58mm 热敏打印。

项目仍在开发中。服务端和固件均有可运行实现，但打印机替换件及完整硬件流程仍需实机验证。

## 架构与目录

```text
ESP32-S3 ──Wi-Fi──> Cloudflare Worker ──> LLM API
                       │                    │
                       ├──> Cloudflare D1 <─┘
                       └──> Vercel Renderer ──> TPB1 384px 位图
                                  │
ESP32-S3 <────────── Worker <─────┘
  ├── SSD1306 预览
  └── 58mm 热敏打印
```

| 目录 | 职责 | 详细说明 |
| --- | --- | --- |
| [`thermal-renderer/`](thermal-renderer/) | Vercel Node 22 服务；渲染 Markdown、公式和表格，输出 PNG 或 TPB1 | [README](thermal-renderer/README.md)、[TPB1 协议](thermal-renderer/docs/tpb1.md) |
| [`thermal-terminal-worker/`](thermal-terminal-worker/) | Cloudflare Worker；鉴权、模型目录、LLM 请求、D1 历史和渲染转发 | [README](thermal-terminal-worker/README.md)、[API](thermal-terminal-worker/docs/api.md) |
| [`thermal-terminal-firmware/`](thermal-terminal-firmware/) | ESP-IDF 固件；OLED、NEC 输入、Wi-Fi、TPB1 接收及打印机驱动 | [README](thermal-terminal-firmware/README.md) |

Worker 位于终端与 Vercel 之间，终端只需访问 Worker 地址。这样终端不必直接连接在部分网络环境下不可用的 Vercel 域名。

## 硬件

当前目标主控为 **ESP32-S3-DevKitC-1-N16R8**（16MB Flash、8MB PSRAM）。OLED 使用 SSD1306 I²C，输入使用 1838B 红外接收头和 21 键 NEC 遥控器；热敏打印机目标型号为 CSN-A2 的 **TTL UART 版本**。板级 GPIO、遥控键映射、OLED 地址和长按阈值集中在 [board_config.hpp](thermal-terminal-firmware/components/board_config/include/board_config/board_config.hpp)。

**不要将 RS232 或超过 ESP32 GPIO 容限的打印机 TXD 直接接入 ESP32。** 当前固件的 `kPrinterInterfaceReady` 默认是 `false`，打印命令会被阻止；确认 TTL 电平、接线和供电后才能开启。打印机电源应按手册单独设计，两节 18650 的电源方案尚需整机验证。

## 快速开始

需要 Node.js 22、npm、Cloudflare Wrangler、Vercel CLI，以及 ESP-IDF 6.1。服务端应按以下顺序部署：

1. **Vercel Renderer**：在 Vercel 创建项目，以 `thermal-renderer` 为 Root Directory；配置 `RENDER_TOKEN`，部署后携带 `Authorization: Bearer <RENDER_TOKEN>` 检查 `GET /health`。依赖安装时会从 npm 包准备 Chromium 和 KaTeX 资源，生成文件不在 Git 中。详见 [渲染服务说明](thermal-renderer/README.md)。
2. **Cloudflare Worker**：在 `thermal-terminal-worker` 中配置 `RENDER_URL` 为 Vercel 项目域名，并设置 `TERMINAL_TOKEN`、与 Vercel 一致的 `RENDER_TOKEN`、`PROVIDERS` Secret；应用 D1 migration 后部署。现有 `wrangler.toml` 含当前 D1 数据库 ID，换 Cloudflare 账号时应先创建自己的 D1 并更新绑定。详见 [Worker 部署说明](thermal-terminal-worker/README.md)。
3. **ESP32 固件**：激活 ESP-IDF 环境，在 `thermal-terminal-firmware` 中构建并完整刷写。完整刷写会同时写入 `assets` SPIFFS 分区中的英文 T9 词典。串口运行 `set_worker <worker_url> <terminal_token>` 配置服务端；Wi-Fi 可在 OLED 设置页扫描、选网和输入密码，也可用串口 `set_wifi`。详见 [固件说明](thermal-terminal-firmware/README.md)。

服务端依赖和部署命令（执行前先在平台配置上述环境变量及 Secret）：

```sh
cd thermal-renderer
npm ci
npx vercel deploy --prod

cd ../thermal-terminal-worker
npm ci
npx wrangler d1 migrations apply DB --remote
npx wrangler deploy
```

```sh
cd thermal-terminal-firmware
source /path/to/esp-idf/export.sh
idf.py -B build-idf build
idf.py -B build-idf -p /dev/ttyACM0 flash monitor
```

将 `/dev/ttyACM0` 替换为实际设备端口。首次刷写或词典更新后请使用完整 `flash`，不要只写应用 `thermal_terminal.bin`。

## 本地验证

```sh
cd thermal-renderer
npm ci
npm test

cd ../thermal-terminal-worker
npm ci
npm test

cd ../thermal-terminal-firmware
cmake -S test/host -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

渲染器端到端测试还需要先启动本地 Vercel 开发服务，步骤见其 README。ESP-IDF 构建和实机测试分别验证交叉编译与硬件行为；主机测试不能代替实机验证。

## 配置与数据

- Renderer 的 `RENDER_TOKEN`、Worker 的 `TERMINAL_TOKEN`/`RENDER_TOKEN`/`PROVIDERS` 均为敏感信息，应通过部署平台 Secret 配置。示例位于 [`thermal-renderer/.env.example`](thermal-renderer/.env.example) 和 [`thermal-terminal-worker/.dev.vars.example`](thermal-terminal-worker/.dev.vars.example)。真实 `.env`、`.dev.vars` 不应提交。
- Worker 在 D1 中保存会话和消息历史。终端设置、Wi-Fi 凭据、Worker URL/Token 和会话 ID 保存在 ESP32 NVS 中；默认收到回复后**不会自动打印**，可在设置中修改。
- 固件英文 T9 词典由 [`en_wordlist.combined`](thermal-terminal-firmware/en_wordlist.combined) 生成并存放在 [`assets/en.t9`](thermal-terminal-firmware/assets/en.t9)；中文笔画输入使用 GB2312 字频表和 16×16 Unifont 字形。字库来源及再生成方法见 [字库说明](thermal-terminal-firmware/components/terminal_ui/data/NOTICE.md)。

项目尚未配置自动化部署；各子项目的部署、接口、遥控器操作和故障排查以其 README 为准。
