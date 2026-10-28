import {releaseResponse} from './releases.js';
export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    if (url.pathname === "/healthz") {
      return new Response(JSON.stringify({ ok: true, service: "at32-ahrs-web-host" }), {
        headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" }
      });
    }
    const release=await releaseResponse(request,env,ctx);if(release)return release;
    return env.ASSETS.fetch(request);
  }
};
