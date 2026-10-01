import { DEFAULT_SELECTION_FALLBACK } from "../config/constants.js";
import { withRetry } from "../utils/helpers.js";

const NEVER_EXPIRES_AT = 8_640_000_000_000_000;
const MAX_HISTORY_PAGE_SIZE = 50;
const CONTEXT_MESSAGE_LIMIT = 10;

let ensureTablesPromise = null;

export async function ensureTables(env) {
  if (!ensureTablesPromise) {
    ensureTablesPromise = (async () => {
      await withRetry(() => env.DB.prepare(
        `CREATE TABLE IF NOT EXISTS sessions (
          id TEXT PRIMARY KEY,
          data TEXT NOT NULL,
          expires_at INTEGER NOT NULL
        )`
      ).run());
      await withRetry(() => env.DB.prepare(
        `CREATE TABLE IF NOT EXISTS terminal_messages (
          id TEXT PRIMARY KEY,
          session_id TEXT NOT NULL,
          sequence INTEGER NOT NULL,
          role TEXT NOT NULL CHECK (role IN ('user', 'assistant')),
          content TEXT NOT NULL,
          usage TEXT,
          created_at INTEGER NOT NULL,
          UNIQUE (session_id, sequence),
          FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
        )`
      ).run());
      await withRetry(() => env.DB.prepare(
        `CREATE INDEX IF NOT EXISTS idx_terminal_messages_session_sequence
         ON terminal_messages (session_id, sequence DESC)`
      ).run());
      return true;
    })().catch((error) => {
      ensureTablesPromise = null;
      throw error;
    });
  }
  return ensureTablesPromise;
}

export function normalizeState(raw, env) {
  return {
    last_selection:
      raw?.last_selection || env.DEFAULT_SELECTION || DEFAULT_SELECTION_FALLBACK,
    use_ctx: raw?.use_ctx !== false,
    reasoning_level: String(raw?.reasoning_level ?? "0")
  };
}

export function defaultSessionState(env) {
  return normalizeState(null, env);
}

function parseState(data, env) {
  try {
    return normalizeState(JSON.parse(data), env);
  } catch {
    return defaultSessionState(env);
  }
}

function parseUsage(value) {
  if (!value) return null;
  if (typeof value === "object") return value;
  try {
    return JSON.parse(value);
  } catch {
    return null;
  }
}

function mapMessage(row) {
  return {
    id: row.id,
    sequence: Number(row.sequence),
    role: row.role,
    content: row.content,
    createdAt: Number(row.created_at),
    usage: parseUsage(row.usage)
  };
}

async function writeSession(env, id, state) {
  await withRetry(() => env.DB.prepare(
    `INSERT INTO sessions (id, data, expires_at)
     VALUES (?, ?, ?)
     ON CONFLICT(id) DO UPDATE SET data = excluded.data, expires_at = excluded.expires_at`
  ).bind(id, JSON.stringify(normalizeState(state, env)), NEVER_EXPIRES_AT).run());
}

async function migrateLegacyMessages(env, sessionId, raw) {
  const legacy = Array.isArray(raw?.messages) ? raw.messages : [];
  if (!legacy.length) return;

  const existing = await withRetry(() => env.DB.prepare(
    `SELECT COUNT(*) AS count FROM terminal_messages WHERE session_id = ?`
  ).bind(sessionId).first());

  if (Number(existing?.count || 0) === 0) {
    const createdAt = Date.now() - legacy.length;
    for (let index = 0; index < legacy.length; index++) {
      const message = legacy[index];
      if (message?.role !== "user" && message?.role !== "assistant") continue;
      const content = String(message.raw_content ?? message.content ?? "");
      await insertMessage(
        env,
        sessionId,
        message.role,
        content,
        message.usage ?? null,
        createdAt + index
      );
    }
  }
}

export async function loadOrCreateSession(env, id) {
  await ensureTables(env);
  const row = await withRetry(() => env.DB.prepare(
    `SELECT data FROM sessions WHERE id = ?`
  ).bind(id).first());

  if (!row) {
    const fresh = defaultSessionState(env);
    await writeSession(env, id, fresh);
    return fresh;
  }

  let raw;
  try {
    raw = JSON.parse(row.data);
  } catch {
    raw = null;
  }
  await migrateLegacyMessages(env, id, raw);
  const state = parseState(row.data, env);
  if (Array.isArray(raw?.messages)) await writeSession(env, id, state);
  return state;
}

export async function saveSessionSettings(env, id, state) {
  await ensureTables(env);
  await writeSession(env, id, state);
}

export async function insertMessage(
  env,
  sessionId,
  role,
  content,
  usage = null,
  createdAt = Date.now()
) {
  if (role !== "user" && role !== "assistant") {
    throw new Error(`Unsupported message role: ${role}`);
  }
  const id = crypto.randomUUID();
  const row = await withRetry(() => env.DB.prepare(
    `INSERT INTO terminal_messages
       (id, session_id, sequence, role, content, usage, created_at)
     VALUES (
       ?, ?,
       COALESCE((SELECT MAX(sequence) + 1 FROM terminal_messages WHERE session_id = ?), 1),
       ?, ?, ?, ?
     )
     RETURNING id, sequence, role, content, usage, created_at`
  ).bind(
    id,
    sessionId,
    sessionId,
    role,
    String(content),
    usage ? JSON.stringify(usage) : null,
    createdAt
  ).first());
  return mapMessage(row);
}

async function queryMessages(env, sql, bindings) {
  const result = await withRetry(() => {
    let statement = env.DB.prepare(sql);
    if (bindings.length) statement = statement.bind(...bindings);
    return statement.all();
  });
  return (result.results || []).map(mapMessage);
}

export async function getContextMessages(
  env,
  sessionId,
  beforeSequence = null
) {
  await ensureTables(env);
  const beforeClause = beforeSequence === null ? "" : " AND sequence < ?";
  const bindings = beforeSequence === null
    ? [sessionId, CONTEXT_MESSAGE_LIMIT]
    : [sessionId, beforeSequence, CONTEXT_MESSAGE_LIMIT];
  const messages = await queryMessages(
    env,
    `SELECT id, sequence, role, content, usage, created_at
     FROM terminal_messages
     WHERE session_id = ?${beforeClause}
     ORDER BY sequence DESC
     LIMIT ?`,
    bindings
  );
  return messages.reverse();
}

export async function listMessages(env, sessionId, options = {}) {
  await ensureTables(env);
  const limit = Math.min(
    MAX_HISTORY_PAGE_SIZE,
    Math.max(1, Number.parseInt(options.limit, 10) || 20)
  );
  const before = options.before === undefined || options.before === null
    ? null
    : Number.parseInt(options.before, 10);
  if (before !== null && (!Number.isSafeInteger(before) || before < 1)) {
    const error = new Error("历史游标无效");
    error.status = 400;
    error.code = "INVALID_CURSOR";
    throw error;
  }

  const beforeClause = before === null ? "" : " AND sequence < ?";
  const bindings = before === null
    ? [sessionId, limit + 1]
    : [sessionId, before, limit + 1];
  const descending = await queryMessages(
    env,
    `SELECT id, sequence, role, content, usage, created_at
     FROM terminal_messages
     WHERE session_id = ?${beforeClause}
     ORDER BY sequence DESC
     LIMIT ?`,
    bindings
  );
  const hasMore = descending.length > limit;
  const page = descending.slice(0, limit);
  const nextCursor = hasMore
    ? String(page[page.length - 1].sequence)
    : null;
  return {
    messages: page.reverse(),
    nextCursor
  };
}

export async function getMessage(env, sessionId, messageId) {
  await ensureTables(env);
  const row = await withRetry(() => env.DB.prepare(
    `SELECT id, sequence, role, content, usage, created_at
     FROM terminal_messages
     WHERE session_id = ? AND id = ?`
  ).bind(sessionId, messageId).first());
  return row ? mapMessage(row) : null;
}

export async function getLastUserMessage(env, sessionId) {
  await ensureTables(env);
  const row = await withRetry(() => env.DB.prepare(
    `SELECT id, sequence, role, content, usage, created_at
     FROM terminal_messages
     WHERE session_id = ? AND role = 'user'
     ORDER BY sequence DESC
     LIMIT 1`
  ).bind(sessionId).first());
  return row ? mapMessage(row) : null;
}

export async function clearMessages(env, sessionId) {
  await ensureTables(env);
  const result = await withRetry(() => env.DB.prepare(
    `DELETE FROM terminal_messages WHERE session_id = ?`
  ).bind(sessionId).run());
  return Number(result.meta?.changes || 0);
}
