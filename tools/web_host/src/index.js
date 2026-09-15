export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/healthz") {
      return new Response(JSON.stringify({ ok: true, service: "at32-ahrs-web-host" }), {
        headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" }
      });
    }
    return env.ASSETS.fetch(request);
  }
};
