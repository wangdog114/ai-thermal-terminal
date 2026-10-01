# Terminal API

基础地址示例：

```text
https://thermal-terminal-worker.example.workers.dev
```

所有接口要求：

```http
Authorization: Bearer <TERMINAL_TOKEN>
```

除首次发送外，客户端应通过 `X-Session-Id` 请求头携带服务端返回的会话 ID。JSON 中的 `sessionId` 也受支持，请求头优先。

## 获取模型目录

```http
GET /api/terminal/config
```

响应只包含公开模型信息，不包含 Provider URL 或 API Key：

```json
{
  "version": 1,
  "defaultModel": "OpenAI:gpt-5.4-mini",
  "models": [
    {
      "id": "OpenAI:gpt-5.4-mini",
      "provider": "OpenAI",
      "name": "gpt-5.4-mini",
      "label": "gpt-5.4-mini",
      "reasoningLevels": [
        { "id": "0", "label": "最低" },
        { "id": "1", "label": "低" }
      ]
    }
  ]
}
```

响应包含 `ETag`，客户端缓存后可发送 `If-None-Match`；目录未变化时返回 304。

## 发送消息

```http
POST /api/terminal
Content-Type: application/json
```

```json
{
  "requestId": "esp32-0001",
  "sessionId": "device-a",
  "content": "请介绍 ESP32",
  "modelSelection": "OpenAI:gpt-5.4-mini",
  "useContext": true,
  "reasoningLevel": "0",
  "format": "tpb",
  "options": {
    "fontSize": 22,
    "lineHeight": 1.45,
    "margin": 12,
    "bottomFeed": 24,
    "threshold": 185,
    "dither": "threshold",
    "compression": "packbits",
    "bandHeight": 256
  }
}
```

`format` 默认为 `tpb`，调试时可设为 `png`。成功响应体是二进制，并包含：

```text
X-Session-Id
X-Request-Id
X-User-Message-Id
X-Assistant-Message-Id
X-TPB-Width
X-TPB-Height
X-TPB-Bands
```

用户消息在 LLM 调用前写入 D1；机器人 Markdown 在渲染前写入 D1。因此 Vercel 临时失败时，历史仍然存在，可以稍后通过消息 ID 重新渲染。

## 重发最后请求

```http
POST /api/terminal/retry
Content-Type: application/json
X-Session-Id: <session-id>
```

请求体可以包含与发送接口相同的模型、上下文、推理和渲染选项，但不传 `content`：

```json
{
  "requestId": "esp32-retry-0001",
  "format": "tpb",
  "options": { "fontSize": 22, "lineHeight": 1.45 }
}
```

重发不会重复保存用户消息，会为最后一条用户消息追加一个新的机器人回复。响应包含：

```text
X-Source-User-Message-Id
X-Assistant-Message-Id
```

没有可重发消息时返回 HTTP 409 和 `NO_MESSAGE_TO_RETRY`。

## 浏览历史

```http
GET /api/terminal/history?limit=20
X-Session-Id: <session-id>
```

首次请求返回最新一页，页内消息按时间正序排列：

```json
{
  "sessionId": "device-a",
  "settings": {
    "modelSelection": "OpenAI:gpt-5.4-mini",
    "useContext": true,
    "reasoningLevel": "0"
  },
  "messages": [
    {
      "id": "...",
      "sequence": 41,
      "role": "user",
      "content": "问题",
      "createdAt": 1789180000000,
      "usage": null
    },
    {
      "id": "...",
      "sequence": 42,
      "role": "assistant",
      "content": "Markdown 回复",
      "createdAt": 1789180001000,
      "usage": { "prompt": 10, "completion": 20, "total": 30 }
    }
  ],
  "nextCursor": "41"
}
```

`limit` 范围为 1 到 50。若 `nextCursor` 不为 `null`，读取更旧一页：

```http
GET /api/terminal/history?limit=20&before=41
```

## 重新渲染历史回复

```http
POST /api/terminal/render-message
Content-Type: application/json
X-Session-Id: <session-id>
```

```json
{
  "requestId": "render-old-001",
  "messageId": "<assistant-message-id>",
  "format": "tpb",
  "options": {
    "fontSize": 18,
    "lineHeight": 1.3,
    "margin": 12,
    "bottomFeed": 24
  }
}
```

该接口只接受 `assistant` 消息，响应体和普通发送相同。客户端改变字号或行距后打印旧回复时使用此接口。

## 清空历史

```http
DELETE /api/terminal/history
X-Session-Id: <session-id>
```

```json
{
  "sessionId": "device-a",
  "cleared": true,
  "deleted": 42
}
```

模型、推理强度和上下文开关不会被重置。客户端应在收到成功响应后再清理本地索引和 PSRAM 缓存。

## 健康检查

```http
GET /api/terminal/health
```

该接口检查 Worker 到 Vercel 的连接，并执行 Vercel Chromium 渲染探针。

## 错误格式

非二进制成功响应采用 JSON：

```json
{
  "error": {
    "code": "MODEL_NOT_FOUND",
    "message": "没有找到指定模型"
  }
}
```

客户端至少应处理 HTTP 400、401、403、404、409、413、422、502、503 和 504。不要把错误 JSON 当作 TPB1 解析；必须先检查 HTTP 状态和 `Content-Type`。
