# Thermal Markdown Renderer

将不可信 Markdown 安全渲染为 384px 宽的 PNG 或 TPB1 单色点阵，供
Cloudflare Worker 转发给 ESP32。生产运行时固定为 Vercel Node 22。

## 配置

复制 `.env.example` 中的变量到 Vercel Project Settings。`RENDER_TOKEN`
必须是足够长的随机值，并与 Cloudflare Worker 使用的值一致。

## API

所有接口都要求：

```http
Authorization: Bearer <RENDER_TOKEN>
```

渲染请求：

```http
POST /render
Content-Type: application/json

{
  "requestId": "optional-id",
  "markdown": "# Markdown",
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

`format` 可为 `tpb` 或仅用于调试的 `png`，查询参数优先于 JSON 字段。
TPB1 的字段定义见 [docs/tpb1.md](docs/tpb1.md)。

深度健康检查使用 `GET /health`。它会实际启动 Chromium、加载中文字体并
渲染最小页面；结果在同一热实例内缓存 60 秒。

公式支持 `$...$`、`$$...$$`、`\(...\)` 和 `\[...\]` 定界符，并会在
可读范围内自动缩放。若公式过宽导致缩放比例低于 55%，接口返回
`FORMULA_TOO_WIDE`（HTTP 422）；Worker 应让模型把公式拆成多行后重试。

默认打印字体为思源黑体 Light（正文）和 Normal（标题、加粗），
不使用浏览器合成粗体。可分别通过 `PRINT_LIGHT_FONT_PATH` 和
`PRINT_FONT_PATH` 指定其他字体文件；所附字体的授权见
`assets/fonts/OFL.txt`。若 Vercel 已设置旧版
`PRINT_FONT_PATH=assets/fonts/SourceHanSansSC-Regular.otf`，请改为
`assets/fonts/SourceHanSansCN-Normal.otf` 或移除该环境变量。

## 验证

```sh
npm test
RENDER_TOKEN=development-token npm run serve
npm run test:e2e
npx vercel build --prod
```

Vercel 只公开 `/render` 和 `/health`，其他路径统一返回 404。
