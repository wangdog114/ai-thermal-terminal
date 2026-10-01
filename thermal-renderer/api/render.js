import crypto from "node:crypto";

import {
  DEBUG_PNG_MAX_HEIGHT,
  MAX_MARKDOWN_CHARS,
  MAX_REQUEST_BYTES,
  normalizeOptions
} from "../src/config.js";

import { renderMarkdownToPng } from "../src/render.js";
import { pngToMonoBitmap } from "../src/raster.js";
import { buildTpbDocument } from "../src/protocol.js";
import { getAssetStatus } from "../src/assets.js";
import { getChromiumStatus } from "../src/browser.js";

export const config = {
  maxDuration: 60
};

const HEALTH_CACHE_TTL_MS = 60_000;

let healthProbePromise = null;
let healthProbeResult = null;
let healthProbeExpiresAt = 0;

function constantTimeEqual(left, right) {
  const leftBuffer = Buffer.from(left);
  const rightBuffer = Buffer.from(right);

  if (leftBuffer.length !== rightBuffer.length) {
    return false;
  }

  return crypto.timingSafeEqual(
    leftBuffer,
    rightBuffer
  );
}

function authorize(request) {
  const expectedToken = process.env.RENDER_TOKEN;

  if (!expectedToken) {
    const error = new Error(
      "服务端未配置 RENDER_TOKEN"
    );
    error.statusCode = 503;
    error.code = "SERVER_NOT_CONFIGURED";
    throw error;
  }

  const authorization =
    request.headers.authorization || "";

  const prefix = "Bearer ";

  if (!authorization.startsWith(prefix)) {
    const error = new Error("缺少渲染服务认证信息");
    error.statusCode = 401;
    error.code = "UNAUTHORIZED";
    throw error;
  }

  const providedToken =
    authorization.slice(prefix.length);

  if (
    !constantTimeEqual(
      providedToken,
      expectedToken
    )
  ) {
    const error = new Error("渲染服务认证失败");
    error.statusCode = 403;
    error.code = "FORBIDDEN";
    throw error;
  }
}

async function parseJsonBody(request) {
  if (
    Number(request.headers["content-length"] || 0) >
    MAX_REQUEST_BYTES
  ) {
    const error = new Error("请求体过大");
    error.statusCode = 413;
    error.code = "REQUEST_TOO_LARGE";
    throw error;
  }

  if (
    request.body &&
    typeof request.body === "object" &&
    !Buffer.isBuffer(request.body)
  ) {
    const serialized = Buffer.from(
      JSON.stringify(request.body),
      "utf8"
    );

    if (serialized.length > MAX_REQUEST_BYTES) {
      const error = new Error("请求体过大");
      error.statusCode = 413;
      error.code = "REQUEST_TOO_LARGE";
      throw error;
    }

    return request.body;
  }

  let raw;

  if (Buffer.isBuffer(request.body)) {
    raw = request.body;
  } else if (typeof request.body === "string") {
    raw = Buffer.from(request.body);
  } else {
    const chunks = [];
    let total = 0;

    for await (const chunk of request) {
      const buffer = Buffer.from(chunk);
      total += buffer.length;

      if (total > MAX_REQUEST_BYTES) {
        const error = new Error("请求体过大");
        error.statusCode = 413;
        error.code = "REQUEST_TOO_LARGE";
        throw error;
      }

      chunks.push(buffer);
    }

    raw = Buffer.concat(chunks);
  }

  if (raw.length > MAX_REQUEST_BYTES) {
    const error = new Error("请求体过大");
    error.statusCode = 413;
    error.code = "REQUEST_TOO_LARGE";
    throw error;
  }

  try {
    return JSON.parse(raw.toString("utf8"));
  } catch {
    const error = new Error("请求体不是有效 JSON");
    error.statusCode = 400;
    error.code = "INVALID_JSON";
    throw error;
  }
}

function sendJsonError(
  response,
  statusCode,
  code,
  message,
  details
) {
  const body = JSON.stringify({
    error: {
      code,
      message,
      ...(details ? { details } : {})
    }
  });

  response.statusCode = statusCode;
  response.setHeader(
    "Content-Type",
    "application/json; charset=utf-8"
  );
  response.setHeader(
    "Cache-Control",
    "no-store"
  );
  response.end(body);
}

function getRequestedFormat(request, body) {
  const url = new URL(
    request.url,
    "https://renderer.local"
  );

  return (
    url.searchParams.get("format") ||
    body.format ||
    "tpb"
  );
}

function isHealthRequest(request) {
  const url = new URL(
    request.url,
    "https://renderer.local"
  );

  return url.searchParams.get("health") === "1";
}

function sendJson(response, statusCode, value) {
  const body = JSON.stringify(value);

  response.statusCode = statusCode;
  response.setHeader(
    "Content-Type",
    "application/json; charset=utf-8"
  );
  response.setHeader("Cache-Control", "no-store");
  response.setHeader("Content-Length", String(Buffer.byteLength(body)));
  response.end(body);
}

async function runHealthProbe() {
  const now = Date.now();

  if (healthProbeResult && now < healthProbeExpiresAt) {
    return healthProbeResult;
  }

  if (!healthProbePromise) {
    healthProbePromise = (async () => {
      const startedAt = Date.now();
      const options = normalizeOptions({
        fontSize: 14,
        lineHeight: 1.2,
        margin: 0,
        bottomFeed: 0
      });

      const rendered = await renderMarkdownToPng(
        "渲染服务就绪",
        options
      );

      const assets = getAssetStatus();
      const chromium = getChromiumStatus();

      const result = {
        checkedAt: new Date().toISOString(),
        durationMs: Date.now() - startedAt,
        probe: {
          width: rendered.width,
          height: rendered.height
        },
        assets: {
          ready: assets.ready,
          assetCount: assets.assetCount
        },
        chromium: {
          ready:
            chromium.packDirectoryExists &&
            chromium.files.every(file => file.exists),
          files: chromium.files.map(file => ({
            fileName: file.fileName,
            exists: file.exists,
            size: file.size
          }))
        }
      };

      healthProbeResult = result;
      healthProbeExpiresAt =
        Date.now() + HEALTH_CACHE_TTL_MS;

      return result;
    })().finally(() => {
      healthProbePromise = null;
    });
  }

  return healthProbePromise;
}

export default async function handler(
  request,
  response
) {
  const startedAt = Date.now();
  let requestId = null;

  try {
    if (isHealthRequest(request)) {
      if (request.method !== "GET") {
        response.setHeader("Allow", "GET");
        sendJsonError(
          response,
          405,
          "METHOD_NOT_ALLOWED",
          "健康检查只允许 GET 请求"
        );
        return;
      }

      authorize(request);

      const probe = await runHealthProbe();

      sendJson(response, 200, {
        ok: true,
        runtime: {
          node: process.version,
          platform: process.platform,
          architecture: process.arch
        },
        width: 384,
        ...probe
      });
      return;
    }

    if (request.method !== "POST") {
      response.setHeader("Allow", "POST");
      sendJsonError(
        response,
        405,
        "METHOD_NOT_ALLOWED",
        "只允许 POST 请求"
      );
      return;
    }

    authorize(request);

    const body = await parseJsonBody(request);

    requestId =
      typeof body.requestId === "string"
        ? body.requestId.slice(0, 128)
        : null;

    if (typeof body.markdown !== "string") {
      const error = new Error(
        "markdown 字段必须是字符串"
      );
      error.statusCode = 400;
      error.code = "INVALID_MARKDOWN";
      throw error;
    }

    if (body.markdown.trim().length === 0) {
      const error = new Error(
        "markdown 字段不能为空"
      );
      error.statusCode = 400;
      error.code = "EMPTY_MARKDOWN";
      throw error;
    }

    if (
      body.markdown.length >
      MAX_MARKDOWN_CHARS
    ) {
      const error = new Error(
        `Markdown 超过 ${MAX_MARKDOWN_CHARS} 字符限制`
      );
      error.statusCode = 413;
      error.code = "MARKDOWN_TOO_LARGE";
      throw error;
    }

    const format = getRequestedFormat(
      request,
      body
    );

    if (!["tpb", "png"].includes(format)) {
      const error = new Error(
        "format 必须是 tpb 或 png"
      );
      error.statusCode = 400;
      error.code = "INVALID_FORMAT";
      throw error;
    }

    const options = normalizeOptions(
      body.options
    );

    const rendered =
      await renderMarkdownToPng(
        body.markdown,
        options
      );

    if (
      format === "png" &&
      rendered.height > DEBUG_PNG_MAX_HEIGHT
    ) {
      const error = new Error(
        `调试 PNG 高度不能超过 ` +
        `${DEBUG_PNG_MAX_HEIGHT}px`
      );
      error.statusCode = 413;
      error.code = "DEBUG_PNG_TOO_TALL";
      throw error;
    }

    response.setHeader(
      "Cache-Control",
      "no-store, no-transform"
    );
    response.setHeader(
      "X-Content-Type-Options",
      "nosniff"
    );
    response.setHeader(
      "X-TPB-Width",
      String(rendered.width)
    );
    response.setHeader(
      "X-TPB-Height",
      String(rendered.height)
    );
    response.setHeader(
      "X-Render-Duration-Ms",
      String(Date.now() - startedAt)
    );

    if (rendered.overflowDetected) {
      response.setHeader(
        "X-Render-Overflow",
        "1"
      );
    }

    if (format === "png") {
      response.statusCode = 200;
      response.setHeader(
        "Content-Type",
        "image/png"
      );
      response.setHeader(
        "Content-Length",
        String(rendered.png.length)
      );
      response.end(rendered.png);
      return;
    }

    const { bitmap } =
      await pngToMonoBitmap(
        rendered.png,
        rendered.width,
        rendered.height,
        options
      );

    const document = buildTpbDocument({
      bitmap,
      width: rendered.width,
      height: rendered.height,
      bandHeight: options.bandHeight,
      compression: options.compression,
      source: body.markdown
    });

    response.statusCode = 200;
    response.setHeader(
      "Content-Type",
      "application/vnd.thermal-bitmap"
    );
    response.setHeader(
      "Content-Length",
      String(document.length)
    );

    response.end(document);
  } catch (error) {
    if (!error.statusCode || error.statusCode >= 500) {
      console.error("Render request failed", {
        requestId,
        code: error.code,
        message: error.message,
        stack: error.stack
      });
    }

    if (response.headersSent) {
      response.destroy(error);
      return;
    }

    sendJsonError(
      response,
      error.statusCode || 500,
      error.code || "INTERNAL_ERROR",
      error.statusCode
        ? error.message
        : "渲染服务内部错误",
      error.details
    );
  }
}
