import { DEFAULT_SELECTION_FALLBACK, SYSTEM_PROMPT } from "./config/constants.js";
import {
  clearMessages,
  getContextMessages,
  getLastUserMessage,
  getMessage,
  insertMessage,
  listMessages,
  loadOrCreateSession,
  saveSessionSettings
} from "./db/session.js";
import { createProviderCompletion } from "./providers/index.js";
import { getModelConfig, parseModelSelection } from "./providers/utils.js";
import { getProviders } from "./utils/helpers.js";

const MAX_REQUEST_BYTES = 256 * 1024;
const DEFAULT_RENDER_TIMEOUT_MS = 55_000;
const IDENTIFIER_PATTERN = /^[A-Za-z0-9._:-]{1,128}$/;
const REASONING_LABELS = ["最低", "低", "中", "高"];

function makeError(status, code, message, details) {
  const error = new Error(message);
  error.status = status;
  error.code = code;
  error.details = details;
  return error;
}

function jsonResponse(value, status = 200, extraHeaders = {}) {
  return new Response(JSON.stringify(value), {
    status,
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "Cache-Control": "no-store",
      ...extraHeaders
    }
  });
}

function jsonError(status, code, message, details) {
  return jsonResponse({
    error: { code, message, ...(details === undefined ? {} : { details }) }
  }, status);
}

function methodNotAllowed(method) {
  return new Response("Method Not Allowed", {
    status: 405,
    headers: { Allow: method, "Cache-Control": "no-store" }
  });
}

function constantTimeEqual(left, right) {
  if (left.length !== right.length) return false;
  let result = 0;
  for (let i = 0; i < left.length; i++) {
    result |= left.charCodeAt(i) ^ right.charCodeAt(i);
  }
  return result === 0;
}

function authorize(request, env) {
  const expected = String(env.TERMINAL_TOKEN || "");
  if (!expected) {
    throw makeError(503, "SERVER_NOT_CONFIGURED", "服务端未配置 TERMINAL_TOKEN");
  }
  const value = request.headers.get("Authorization") || "";
  if (!value.startsWith("Bearer ")) {
    throw makeError(401, "UNAUTHORIZED", "缺少终端认证信息");
  }
  if (!constantTimeEqual(value.slice(7), expected)) {
    throw makeError(403, "FORBIDDEN", "终端认证失败");
  }
}

async function readJson(request) {
  const contentLength = Number(request.headers.get("Content-Length") || 0);
  if (contentLength > MAX_REQUEST_BYTES) {
    throw makeError(413, "REQUEST_TOO_LARGE", "请求体过大");
  }
  let raw;
  try {
    raw = await request.arrayBuffer();
  } catch {
    throw makeError(400, "INVALID_JSON", "无法读取请求体");
  }
  if (raw.byteLength > MAX_REQUEST_BYTES) {
    throw makeError(413, "REQUEST_TOO_LARGE", "请求体过大");
  }
  try {
    const value = JSON.parse(new TextDecoder().decode(raw));
    if (!value || typeof value !== "object" || Array.isArray(value)) {
      throw new Error("object expected");
    }
    return value;
  } catch {
    throw makeError(400, "INVALID_JSON", "请求体不是有效 JSON");
  }
}

function validateIdentifier(value, name, code) {
  const text = String(value || "");
  if (!IDENTIFIER_PATTERN.test(text)) {
    throw makeError(400, code, `${name} 格式无效`);
  }
  return text;
}

function getSessionId(request, body, required = false) {
  const url = new URL(request.url);
  const candidate =
    request.headers.get("X-Session-Id") ||
    body?.sessionId ||
    body?.session_id ||
    url.searchParams.get("sessionId");
  if (candidate === undefined || candidate === null || candidate === "") {
    if (required) {
      throw makeError(400, "SESSION_ID_REQUIRED", "该操作需要 sessionId");
    }
    return crypto.randomUUID();
  }
  return validateIdentifier(candidate, "sessionId", "INVALID_SESSION_ID");
}

function getRequestId(body) {
  const candidate = body.requestId || body.request_id;
  return candidate
    ? validateIdentifier(candidate, "requestId", "INVALID_REQUEST_ID")
    : crypto.randomUUID();
}

function getUserText(body) {
  const value = body.content ?? body.message ?? body.prompt ?? body.userText;
  if (typeof value !== "string" || !value.trim()) {
    throw makeError(400, "EMPTY_MESSAGE", "缺少待发送的消息内容");
  }
  if (value.length > 100_000) {
    throw makeError(413, "MESSAGE_TOO_LARGE", "消息内容过长");
  }
  return value.trim();
}

function getSelection(body, state, env) {
  const value =
    body.modelSelection ||
    body.model_selection ||
    body.model ||
    state.last_selection ||
    env.DEFAULT_SELECTION ||
    DEFAULT_SELECTION_FALLBACK;
  if (typeof value !== "string" || !value.trim()) {
    throw makeError(400, "INVALID_MODEL", "模型选择无效");
  }
  return value.trim();
}

function boolValue(value, fallback) {
  if (value === undefined || value === null) return fallback;
  if (typeof value === "boolean") return value;
  return value === "1" || value === "true" || value === "on";
}

function reasoningValues(providerInfo, modelInfo) {
  const values =
    modelInfo?.reasoning_effort ??
    modelInfo?.reasoningEffort ??
    providerInfo.reasoning_effort ??
    providerInfo.reasoningEffort;
  return Array.isArray(values) ? values : [];
}

function resolveModel(body, state, env) {
  const selection = getSelection(body, state, env);
  const providers = getProviders(env);
  const [providerName, modelName] = parseModelSelection(selection);
  const providerInfo = providers[providerName];
  if (!providerInfo) {
    throw makeError(400, "PROVIDER_NOT_FOUND", `未找到模型提供商：${providerName}`);
  }
  const modelInfo = getModelConfig(providerInfo, modelName);
  if (!modelInfo) {
    throw makeError(400, "MODEL_NOT_FOUND", `未找到模型：${selection}`);
  }
  return { selection, providerName, modelName, providerInfo, modelInfo };
}

function getReasoningLevel(body, state, model) {
  const value = String(
    body.reasoningLevel ?? body.reasoning_level ?? state.reasoning_level ?? "0"
  );
  if (!/^\d+$/.test(value)) {
    throw makeError(400, "INVALID_REASONING_LEVEL", "推理强度无效");
  }
  const levels = reasoningValues(model.providerInfo, model.modelInfo);
  const index = Number(value);
  if ((levels.length && index >= levels.length) || (!levels.length && index !== 0)) {
    throw makeError(400, "INVALID_REASONING_LEVEL", "当前模型不支持该推理强度");
  }
  return value;
}

async function renderAtVercel(markdown, body, env, requestId) {
  const renderUrl = String(env.RENDER_URL || "").replace(/\/+$/, "");
  const renderToken = String(env.RENDER_TOKEN || "");
  if (!renderUrl || !renderToken) {
    throw makeError(
      503,
      "RENDER_NOT_CONFIGURED",
      "服务端未配置 RENDER_URL 或 RENDER_TOKEN"
    );
  }
  const format = body.format === "png" ? "png" : "tpb";
  const timeoutMs = Math.min(
    60_000,
    Math.max(5_000, Number(env.RENDER_TIMEOUT_MS) || DEFAULT_RENDER_TIMEOUT_MS)
  );
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(`${renderUrl}/render?format=${format}`, {
      method: "POST",
      headers: {
        Authorization: `Bearer ${renderToken}`,
        "Content-Type": "application/json",
        Accept: format === "tpb" ? "application/octet-stream" : "image/png"
      },
      body: JSON.stringify({
        requestId,
        markdown,
        format,
        options:
          body.options && typeof body.options === "object" && !Array.isArray(body.options)
            ? body.options
            : {}
      }),
      signal: controller.signal
    });
    if (!response.ok) {
      const text = await response.text();
      let details = text;
      try {
        details = JSON.parse(text);
      } catch {
        // Keep the upstream text when it is not JSON.
      }
      throw makeError(
        response.status >= 500 ? 502 : response.status,
        "RENDER_UPSTREAM_ERROR",
        "Vercel 渲染服务返回错误",
        details
      );
    }
    const data = await response.arrayBuffer();
    if (!data.byteLength) {
      throw makeError(502, "RENDER_EMPTY_RESPONSE", "渲染服务返回空内容");
    }
    if (
      format === "tpb" &&
      new TextDecoder().decode(data.slice(0, 4)) !== "TPB1"
    ) {
      throw makeError(502, "RENDER_INVALID_RESPONSE", "渲染服务返回的不是 TPB1 数据");
    }
    return { data, format, headers: response.headers };
  } catch (error) {
    if (error?.name === "AbortError") {
      throw makeError(504, "RENDER_TIMEOUT", "渲染服务请求超时");
    }
    throw error;
  } finally {
    clearTimeout(timeout);
  }
}

function copyRenderHeaders(source, target) {
  for (const name of [
    "X-TPB-Width",
    "X-TPB-Height",
    "X-TPB-Bands",
    "X-TPB-Raw-Length",
    "X-TPB-Document-Length",
    "X-Render-Request-Id"
  ]) {
    const value = source.get(name);
    if (value) target.set(name, value);
  }
}

function binaryResponse(rendered, metadata) {
  const headers = new Headers({
    "Content-Type":
      rendered.format === "tpb" ? "application/octet-stream" : "image/png",
    "Cache-Control": "no-store",
    "Content-Length": String(rendered.data.byteLength),
    "X-Session-Id": metadata.sessionId,
    "X-Request-Id": metadata.requestId,
    "X-Assistant-Message-Id": metadata.assistantMessageId
  });
  if (metadata.userMessageId) {
    headers.set("X-User-Message-Id", metadata.userMessageId);
  }
  if (metadata.sourceUserMessageId) {
    headers.set("X-Source-User-Message-Id", metadata.sourceUserMessageId);
  }
  copyRenderHeaders(rendered.headers, headers);
  return new Response(rendered.data, { status: 200, headers });
}

async function complete(model, history, userText, reasoningLevel, env) {
  const result = await createProviderCompletion(
    model.providerName,
    model.providerInfo,
    model.modelInfo,
    model.modelName,
    SYSTEM_PROMPT,
    history.map((message) => ({
      role: message.role,
      raw_content: message.content,
      content: message.content
    })),
    userText,
    reasoningLevel,
    env
  );
  if (typeof result.text !== "string" || !result.text.trim()) {
    throw makeError(502, "LLM_EMPTY_RESPONSE", "模型返回了空内容");
  }
  return result;
}

async function runHandler(name, action) {
  try {
    return await action();
  } catch (error) {
    const status = Number(error?.status) || 500;
    const code = error?.code || "INTERNAL_ERROR";
    if (status >= 500) console.error(`[${name}] ${code}:`, error?.stack || error);
    return jsonError(status, code, error?.message || "Worker 内部错误", error?.details);
  }
}

export async function handleTerminalRequest(request, env) {
  if (request.method !== "POST") return methodNotAllowed("POST");
  return runHandler("terminal", async () => {
    authorize(request, env);
    const body = await readJson(request);
    const sessionId = getSessionId(request, body);
    const state = await loadOrCreateSession(env, sessionId);
    const userText = getUserText(body);
    const model = resolveModel(body, state, env);
    const useContext = boolValue(body.useContext ?? body.use_ctx, state.use_ctx);
    const reasoningLevel = getReasoningLevel(body, state, model);
    const requestId = getRequestId(body);
    const history = useContext ? await getContextMessages(env, sessionId) : [];

    state.last_selection = model.selection;
    state.use_ctx = useContext;
    state.reasoning_level = reasoningLevel;
    await saveSessionSettings(env, sessionId, state);

    const userMessage = await insertMessage(env, sessionId, "user", userText);
    const result = await complete(model, history, userText, reasoningLevel, env);
    const assistantMessage = await insertMessage(
      env,
      sessionId,
      "assistant",
      result.text,
      result.usage
    );
    const rendered = await renderAtVercel(result.text, body, env, requestId);
    return binaryResponse(rendered, {
      sessionId,
      requestId,
      userMessageId: userMessage.id,
      assistantMessageId: assistantMessage.id
    });
  });
}

export async function handleTerminalRetry(request, env) {
  if (request.method !== "POST") return methodNotAllowed("POST");
  return runHandler("terminal-retry", async () => {
    authorize(request, env);
    const body = await readJson(request);
    const sessionId = getSessionId(request, body, true);
    const state = await loadOrCreateSession(env, sessionId);
    const source = await getLastUserMessage(env, sessionId);
    if (!source) {
      throw makeError(409, "NO_MESSAGE_TO_RETRY", "当前会话没有可重发的用户消息");
    }
    const model = resolveModel(body, state, env);
    const useContext = boolValue(body.useContext ?? body.use_ctx, state.use_ctx);
    const reasoningLevel = getReasoningLevel(body, state, model);
    const requestId = getRequestId(body);
    const history = useContext
      ? await getContextMessages(env, sessionId, source.sequence)
      : [];

    state.last_selection = model.selection;
    state.use_ctx = useContext;
    state.reasoning_level = reasoningLevel;
    await saveSessionSettings(env, sessionId, state);

    const result = await complete(model, history, source.content, reasoningLevel, env);
    const assistantMessage = await insertMessage(
      env,
      sessionId,
      "assistant",
      result.text,
      result.usage
    );
    const rendered = await renderAtVercel(result.text, body, env, requestId);
    return binaryResponse(rendered, {
      sessionId,
      requestId,
      sourceUserMessageId: source.id,
      assistantMessageId: assistantMessage.id
    });
  });
}

export async function handleTerminalHistory(request, env) {
  if (request.method !== "GET" && request.method !== "DELETE") {
    return methodNotAllowed("GET, DELETE");
  }
  return runHandler("terminal-history", async () => {
    authorize(request, env);
    const sessionId = getSessionId(request, null, true);
    const state = await loadOrCreateSession(env, sessionId);
    if (request.method === "DELETE") {
      const deleted = await clearMessages(env, sessionId);
      return jsonResponse({ sessionId, cleared: true, deleted });
    }
    const url = new URL(request.url);
    const page = await listMessages(env, sessionId, {
      before: url.searchParams.get("before"),
      limit: url.searchParams.get("limit")
    });
    return jsonResponse({
      sessionId,
      settings: {
        modelSelection: state.last_selection,
        useContext: state.use_ctx,
        reasoningLevel: state.reasoning_level
      },
      ...page
    });
  });
}

export async function handleTerminalRenderMessage(request, env) {
  if (request.method !== "POST") return methodNotAllowed("POST");
  return runHandler("terminal-render-message", async () => {
    authorize(request, env);
    const body = await readJson(request);
    const sessionId = getSessionId(request, body, true);
    const messageId = validateIdentifier(
      body.messageId || body.message_id,
      "messageId",
      "INVALID_MESSAGE_ID"
    );
    const message = await getMessage(env, sessionId, messageId);
    if (!message) {
      throw makeError(404, "MESSAGE_NOT_FOUND", "没有找到指定消息");
    }
    if (message.role !== "assistant") {
      throw makeError(409, "MESSAGE_NOT_RENDERABLE", "只能重新渲染机器人回复");
    }
    const requestId = getRequestId(body);
    const rendered = await renderAtVercel(message.content, body, env, requestId);
    return binaryResponse(rendered, {
      sessionId,
      requestId,
      assistantMessageId: message.id
    });
  });
}

export async function handleTerminalConfig(request, env) {
  if (request.method !== "GET") return methodNotAllowed("GET");
  return runHandler("terminal-config", async () => {
    authorize(request, env);
    const providers = getProviders(env);
    const models = [];
    for (const [providerName, providerInfo] of Object.entries(providers)) {
      for (const modelInfo of providerInfo.models || []) {
        const name = String(modelInfo.name || "");
        if (!name) continue;
        const values = reasoningValues(providerInfo, modelInfo);
        models.push({
          id: `${providerName}:${name}`,
          provider: providerName,
          name,
          label: String(
            modelInfo.label || modelInfo.display_name || modelInfo.displayName || name
          ),
          reasoningLevels: values.length
            ? values.map((_, index) => ({
                id: String(index),
                label: REASONING_LABELS[index] || `级别 ${index}`
              }))
            : [{ id: "0", label: "默认" }]
        });
      }
    }
    const configuredDefault = String(env.DEFAULT_SELECTION || DEFAULT_SELECTION_FALLBACK);
    const defaultModel = models.some((model) => model.id === configuredDefault)
      ? configuredDefault
      : models[0]?.id || null;
    const body = JSON.stringify({ version: 1, defaultModel, models });
    const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(body));
    const etag = `"${Array.from(new Uint8Array(digest).slice(0, 8), (byte) =>
      byte.toString(16).padStart(2, "0")
    ).join("")}"`;
    if (request.headers.get("If-None-Match") === etag) {
      return new Response(null, { status: 304, headers: { ETag: etag } });
    }
    return new Response(body, {
      status: 200,
      headers: {
        "Content-Type": "application/json; charset=utf-8",
        "Cache-Control": "private, max-age=300",
        ETag: etag
      }
    });
  });
}

export async function handleTerminalHealth(request, env) {
  if (request.method !== "GET") return methodNotAllowed("GET");
  return runHandler("terminal-health", async () => {
    authorize(request, env);
    const renderUrl = String(env.RENDER_URL || "").replace(/\/+$/, "");
    const renderToken = String(env.RENDER_TOKEN || "");
    if (!renderUrl || !renderToken) {
      throw makeError(
        503,
        "RENDER_NOT_CONFIGURED",
        "服务端未配置 RENDER_URL 或 RENDER_TOKEN"
      );
    }
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 10_000);
    let response;
    try {
      response = await fetch(`${renderUrl}/health`, {
        headers: { Authorization: `Bearer ${renderToken}` },
        signal: controller.signal
      });
    } catch (error) {
      if (error?.name === "AbortError") {
        throw makeError(504, "RENDER_TIMEOUT", "渲染服务健康检查超时");
      }
      throw error;
    } finally {
      clearTimeout(timeout);
    }
    const text = await response.text();
    let renderer;
    try {
      renderer = JSON.parse(text);
    } catch {
      renderer = { raw: text.slice(0, 1000) };
    }
    return jsonResponse({ ok: response.ok, renderer }, response.ok ? 200 : 502);
  });
}
