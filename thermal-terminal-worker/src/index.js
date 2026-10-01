import {
  handleTerminalConfig,
  handleTerminalHealth,
  handleTerminalHistory,
  handleTerminalRenderMessage,
  handleTerminalRequest,
  handleTerminalRetry
} from "./terminal.js";

const routes = new Map([
  ["/api/terminal", handleTerminalRequest],
  ["/api/terminal/config", handleTerminalConfig],
  ["/api/terminal/health", handleTerminalHealth],
  ["/api/terminal/history", handleTerminalHistory],
  ["/api/terminal/render-message", handleTerminalRenderMessage],
  ["/api/terminal/retry", handleTerminalRetry]
]);

export default {
  async fetch(request, env) {
    if (!env.DB) {
      return new Response("D1 binding 'DB' is not configured", { status: 500 });
    }
    const handler = routes.get(new URL(request.url).pathname);
    if (handler) return handler(request, env);
    return new Response("Not Found", {
      status: 404,
      headers: { "Cache-Control": "no-store" }
    });
  }
};
