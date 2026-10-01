import assert from "node:assert/strict";
import test from "node:test";

import {
  handleTerminalConfig,
  handleTerminalHealth,
  handleTerminalHistory,
  handleTerminalRenderMessage,
  handleTerminalRequest,
  handleTerminalRetry
} from "../src/terminal.js";
import { loadOrCreateSession } from "../src/db/session.js";

function rowFor(message) {
  return {
    id: message.id,
    sequence: message.sequence,
    role: message.role,
    content: message.content,
    usage: message.usage,
    created_at: message.createdAt
  };
}

function createDb() {
  const sessions = new Map();
  const messages = [];

  function statement(sql) {
    const normalized = sql.replace(/\s+/g, " ").trim();
    return {
      args: [],
      bind(...args) {
        this.args = args;
        return this;
      },
      async run() {
        if (normalized.startsWith("INSERT INTO sessions")) {
          sessions.set(this.args[0], {
            data: this.args[1],
            expires_at: this.args[2]
          });
          return { meta: { changes: 1 } };
        }
        if (normalized.startsWith("DELETE FROM terminal_messages")) {
          const before = messages.length;
          for (let i = messages.length - 1; i >= 0; i--) {
            if (messages[i].sessionId === this.args[0]) messages.splice(i, 1);
          }
          return { meta: { changes: before - messages.length } };
        }
        return { meta: { changes: 0 } };
      },
      async first() {
        if (normalized.startsWith("SELECT data FROM sessions")) {
          return sessions.get(this.args[0]) || null;
        }
        if (normalized.startsWith("SELECT COUNT(*) AS count")) {
          return { count: messages.filter((item) => item.sessionId === this.args[0]).length };
        }
        if (normalized.startsWith("INSERT INTO terminal_messages")) {
          const [id, sessionId, , role, content, usage, createdAt] = this.args;
          const sequence = messages
            .filter((item) => item.sessionId === sessionId)
            .reduce((max, item) => Math.max(max, item.sequence), 0) + 1;
          const message = { id, sessionId, sequence, role, content, usage, createdAt };
          messages.push(message);
          return rowFor(message);
        }
        if (normalized.includes("WHERE session_id = ? AND id = ?")) {
          const found = messages.find(
            (item) => item.sessionId === this.args[0] && item.id === this.args[1]
          );
          return found ? rowFor(found) : null;
        }
        if (normalized.includes("AND role = 'user'")) {
          const found = messages
            .filter((item) => item.sessionId === this.args[0] && item.role === "user")
            .sort((left, right) => right.sequence - left.sequence)[0];
          return found ? rowFor(found) : null;
        }
        return null;
      },
      async all() {
        const hasBefore = normalized.includes("AND sequence < ?");
        const sessionId = this.args[0];
        const before = hasBefore ? this.args[1] : null;
        const limit = this.args[hasBefore ? 2 : 1];
        const results = messages
          .filter(
            (item) =>
              item.sessionId === sessionId &&
              (before === null || item.sequence < before)
          )
          .sort((left, right) => right.sequence - left.sequence)
          .slice(0, limit)
          .map(rowFor);
        return { results };
      }
    };
  }

  return { prepare: statement, sessions, messages };
}

function baseEnv() {
  return {
    DB: createDb(),
    TERMINAL_TOKEN: "terminal-test-token",
    RENDER_URL: "https://renderer.test",
    RENDER_TOKEN: "renderer-test-token",
    PROVIDERS: {
      OpenAI: {
        base_url: "https://api.openai.test/v1",
        api_key: "llm-test-key",
        models: [
          {
            name: "test-model",
            label: "Test Model",
            api_type: "completions",
            reasoning_effort: ["low", "medium", "high"]
          }
        ]
      }
    },
    DEFAULT_SELECTION: "OpenAI:test-model"
  };
}

function authorizedRequest(url, init = {}) {
  const headers = new Headers(init.headers);
  headers.set("Authorization", "Bearer terminal-test-token");
  return new Request(url, { ...init, headers });
}

function jsonRequest(url, value, method = "POST") {
  return authorizedRequest(url, {
    method,
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(value)
  });
}

test("terminal API rejects missing authentication", async () => {
  const response = await handleTerminalRequest(
    new Request("https://worker.test/api/terminal", {
      method: "POST",
      body: JSON.stringify({ content: "hello" }),
      headers: { "Content-Type": "application/json" }
    }),
    baseEnv()
  );
  assert.equal(response.status, 401);
  assert.equal((await response.json()).error.code, "UNAUTHORIZED");
});

test("model catalog is sanitized and supports ETag", async () => {
  const env = baseEnv();
  const response = await handleTerminalConfig(
    authorizedRequest("https://worker.test/api/terminal/config"),
    env
  );
  assert.equal(response.status, 200);
  const catalog = await response.json();
  assert.equal(catalog.defaultModel, "OpenAI:test-model");
  assert.deepEqual(catalog.models[0].reasoningLevels.map((item) => item.id), ["0", "1", "2"]);
  assert.doesNotMatch(JSON.stringify(catalog), /llm-test-key|api.openai.test/);

  const cached = await handleTerminalConfig(
    authorizedRequest("https://worker.test/api/terminal/config", {
      headers: { "If-None-Match": response.headers.get("ETag") }
    }),
    env
  );
  assert.equal(cached.status, 304);
});

test("terminal health proxies renderer health", async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async (url, options) => {
    assert.equal(url, "https://renderer.test/health");
    assert.equal(options.headers.Authorization, "Bearer renderer-test-token");
    return new Response(JSON.stringify({ chromium: { ready: true } }), { status: 200 });
  };
  try {
    const response = await handleTerminalHealth(
      authorizedRequest("https://worker.test/api/terminal/health"),
      baseEnv()
    );
    assert.equal(response.status, 200);
    assert.equal((await response.json()).renderer.chromium.ready, true);
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("legacy JSON messages migrate into normalized history", async () => {
  const env = baseEnv();
  env.DB.sessions.set("legacy-session", {
    data: JSON.stringify({
      last_selection: "OpenAI:test-model",
      use_ctx: true,
      reasoning_level: "2",
      messages: [
        { role: "user", content: "escaped", raw_content: "old question" },
        { role: "assistant", content: "escaped", raw_content: "old answer" }
      ]
    }),
    expires_at: Date.now() - 1
  });

  const state = await loadOrCreateSession(env, "legacy-session");
  assert.equal(state.reasoning_level, "2");
  assert.deepEqual(env.DB.messages.map((item) => item.content), ["old question", "old answer"]);
  assert.equal("messages" in JSON.parse(env.DB.sessions.get("legacy-session").data), false);
});

test("send, history, rerender, retry and clear form one session workflow", async () => {
  const env = baseEnv();
  const originalFetch = globalThis.fetch;
  const calls = [];
  let completionNumber = 0;
  globalThis.fetch = async (url, options) => {
    calls.push({ url: String(url), body: options?.body });
    if (String(url).includes("api.openai.test")) {
      completionNumber++;
      return new Response(
        `data: {"choices":[{"delta":{"content":"Answer ${completionNumber}"}}]}\n\ndata: [DONE]\n`,
        { status: 200 }
      );
    }
    return new Response(new Uint8Array([0x54, 0x50, 0x42, 0x31, completionNumber]), {
      status: 200,
      headers: {
        "Content-Type": "application/octet-stream",
        "X-TPB-Width": "384",
        "X-TPB-Height": "1"
      }
    });
  };

  try {
    const sent = await handleTerminalRequest(
      jsonRequest("https://worker.test/api/terminal", {
        sessionId: "device-1",
        content: "Question",
        modelSelection: "OpenAI:test-model",
        reasoningLevel: "1"
      }),
      env
    );
    assert.equal(sent.status, 200);
    assert.equal(sent.headers.get("X-Session-Id"), "device-1");
    assert.ok(sent.headers.get("X-User-Message-Id"));
    const assistantId = sent.headers.get("X-Assistant-Message-Id");
    assert.ok(assistantId);
    assert.deepEqual([...new Uint8Array(await sent.arrayBuffer())], [0x54, 0x50, 0x42, 0x31, 1]);

    const historyResponse = await handleTerminalHistory(
      authorizedRequest("https://worker.test/api/terminal/history?sessionId=device-1&limit=20"),
      env
    );
    assert.equal(historyResponse.status, 200);
    const history = await historyResponse.json();
    assert.deepEqual(history.messages.map((item) => [item.role, item.content]), [
      ["user", "Question"],
      ["assistant", "Answer 1"]
    ]);
    assert.equal(history.settings.reasoningLevel, "1");

    const rerendered = await handleTerminalRenderMessage(
      jsonRequest("https://worker.test/api/terminal/render-message", {
        sessionId: "device-1",
        messageId: assistantId,
        options: { fontSize: 18 }
      }),
      env
    );
    assert.equal(rerendered.status, 200);
    assert.equal(rerendered.headers.get("X-Assistant-Message-Id"), assistantId);
    const renderBody = JSON.parse(calls.at(-1).body);
    assert.equal(renderBody.markdown, "Answer 1");
    assert.equal(renderBody.options.fontSize, 18);

    const retried = await handleTerminalRetry(
      jsonRequest("https://worker.test/api/terminal/retry", {
        sessionId: "device-1"
      }),
      env
    );
    assert.equal(retried.status, 200);
    assert.equal(retried.headers.get("X-Source-User-Message-Id"), sent.headers.get("X-User-Message-Id"));
    await retried.arrayBuffer();
    assert.equal(env.DB.messages.filter((item) => item.role === "user").length, 1);
    assert.equal(env.DB.messages.filter((item) => item.role === "assistant").length, 2);

    const newestPageResponse = await handleTerminalHistory(
      authorizedRequest("https://worker.test/api/terminal/history?sessionId=device-1&limit=2"),
      env
    );
    const newestPage = await newestPageResponse.json();
    assert.deepEqual(newestPage.messages.map((item) => item.sequence), [2, 3]);
    assert.equal(newestPage.nextCursor, "2");

    const olderPageResponse = await handleTerminalHistory(
      authorizedRequest(
        `https://worker.test/api/terminal/history?sessionId=device-1&limit=2&before=${newestPage.nextCursor}`
      ),
      env
    );
    const olderPage = await olderPageResponse.json();
    assert.deepEqual(olderPage.messages.map((item) => item.sequence), [1]);
    assert.equal(olderPage.nextCursor, null);

    const cleared = await handleTerminalHistory(
      authorizedRequest("https://worker.test/api/terminal/history?sessionId=device-1", {
        method: "DELETE"
      }),
      env
    );
    assert.equal(cleared.status, 200);
    assert.deepEqual(await cleared.json(), {
      sessionId: "device-1",
      cleared: true,
      deleted: 3
    });
    assert.equal(env.DB.messages.length, 0);
  } finally {
    globalThis.fetch = originalFetch;
  }
});
