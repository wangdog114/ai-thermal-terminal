# Thermal Terminal Worker

Cloudflare Worker 端负责调用 LLM、在 D1 保存终端会话历史，并将模型生成的 Markdown 转交给 Vercel 渲染为 TPB1 点阵。

完整接口契约见 [docs/api.md](docs/api.md)。

## 环境变量

- `TERMINAL_TOKEN`：ESP32 调用 Worker 的 Bearer Token，必须使用 Secret。
- `RENDER_URL`：Vercel 渲染服务地址。
- `RENDER_TOKEN`：Vercel 渲染服务 Token，必须使用 Secret。
- `PROVIDERS`：提供商及模型配置 JSON，必须使用 Secret，接口只下发脱敏后的模型目录。
- `DEFAULT_SELECTION`：可选，默认模型 ID，例如 `OpenAI:gpt-5.4-mini`。
- `RENDER_TIMEOUT_MS`：可选，渲染超时，默认 55000，范围 5000 到 60000。

本地开发可以复制 `.dev.vars.example` 为不提交的 `.dev.vars`。

## 数据策略

- `sessions` 保存当前模型、推理强度和上下文开关。
- `terminal_messages` 逐条保存用户消息和机器人 Markdown。
- 历史不会被自动截断或定时删除，只由终端调用清空接口删除。
- LLM 上下文只使用当前请求之前最近 10 条消息，以控制 Token 成本。
- 旧版本保存在 `sessions.data.messages` 中的消息会在首次访问该会话时自动迁移。

## 部署

```sh
npm install
npx wrangler secret put TERMINAL_TOKEN
npx wrangler secret put RENDER_TOKEN
npx wrangler secret put PROVIDERS
npx wrangler d1 migrations apply DB --remote
npx wrangler deploy
```

`RENDER_URL` 和 `DEFAULT_SELECTION` 等非敏感值可以在 Cloudflare Dashboard 配置，或放入 `wrangler.toml` 的 `[vars]`。

本地检查：

```sh
npm test
npx wrangler d1 migrations apply DB --local
npx wrangler deploy --dry-run
```
