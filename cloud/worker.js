// Kältekammer status backend — Cloudflare Worker + KV.
//
// Routes:
//   POST /ingest   device pushes status JSON (Authorization: Bearer <token>)
//   GET  /data     latest status JSON (public, read-only)
//   GET  /         public viewer page (auto-refreshing)
//
// Setup (see cloud/README.md):
//   wrangler kv namespace create STATUS_KV   # put the id in wrangler.toml
//   wrangler secret put INGEST_TOKEN          # must match firmware TELEMETRY_TOKEN
//   wrangler deploy

const KV_KEY = "latest";

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // ── Device ingest ──────────────────────────────────────────────
    if (url.pathname === "/ingest" && request.method === "POST") {
      const auth = request.headers.get("authorization") || "";
      const token = auth.replace(/^Bearer\s+/i, "");
      if (!env.INGEST_TOKEN || token !== env.INGEST_TOKEN) {
        return json({ error: "unauthorized" }, 401);
      }
      let body;
      try { body = await request.json(); }
      catch { return json({ error: "invalid json" }, 400); }

      const record = { received_at: Date.now(), status: body };
      await env.STATUS_KV.put(KV_KEY, JSON.stringify(record));
      return json({ ok: true });
    }

    // ── Public latest-status JSON ──────────────────────────────────
    if (url.pathname === "/data" && request.method === "GET") {
      const raw = await env.STATUS_KV.get(KV_KEY);
      if (!raw) return json({ error: "no data yet" }, 404);
      return new Response(raw, {
        headers: { "content-type": "application/json", "cache-control": "no-store" },
      });
    }

    // ── Public viewer page ─────────────────────────────────────────
    if (url.pathname === "/" && request.method === "GET") {
      return new Response(VIEWER_HTML, {
        headers: { "content-type": "text/html; charset=utf-8" },
      });
    }

    return json({ error: "not found" }, 404);
  },
};

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { "content-type": "application/json", "cache-control": "no-store" },
  });
}

const VIEWER_HTML = `<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kältekammer Samosir — Online-Status</title>
<style>
  body{font-family:system-ui,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:1rem;max-width:760px;margin:0 auto}
  h1{font-size:1.1rem}
  .age{font-size:.85rem;color:#8b949e}
  .age.stale{color:#f85149;font-weight:600}
  .grid{display:grid;grid-template-columns:repeat(2,1fr);gap:.7rem;margin-top:1rem}
  .card{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:.8rem}
  .card h2{font-size:.7rem;letter-spacing:.08em;color:#8b949e;margin:0 0 .4rem}
  .big{font-size:1.6rem;font-weight:700;color:#6cb6ff;font-variant-numeric:tabular-nums}
  .row{font-size:.85rem;margin-top:.2rem}
  .row b{color:#58a6ff}
  .demo{background:#f0883e;color:#1a1207;font-weight:700;padding:.1rem .5rem;border-radius:999px;font-size:.7rem}
  .alarm{color:#f85149;font-weight:600}
</style></head><body>
<h1>❄ Kältekammer Samosir — Online-Status <span id="demo" class="demo" hidden>DEMO</span></h1>
<div class="age" id="age">lädt…</div>
<div class="grid">
  <div class="card"><h2>KAMMER</h2><div class="big" id="tk">--,- °C</div>
    <div class="row">Modus: <b id="mode">–</b></div><div class="row">RH: <b id="rh">–</b></div></div>
  <div class="card"><h2>KOMPRESSOR</h2><div class="big" id="comp">–</div>
    <div class="row"><b id="amps">– A</b></div><div class="row">EVI: <b id="evi">–</b></div></div>
  <div class="card"><h2>SOLAR PV</h2><div class="big" id="pv">---- W</div>
    <div class="row">Grid: <b id="grid">–</b></div><div class="row">Heute: <b id="day">–</b></div></div>
  <div class="card"><h2>WÄRME</h2>
    <div class="row">WW: <b id="ww">–</b></div><div class="row">Pool: <b id="pool">–</b></div>
    <div class="row">Außen: <b id="out">–</b></div></div>
</div>
<div class="card" style="margin-top:.7rem"><h2>ALARME</h2><div id="alarms">–</div></div>
<div class="age" style="margin-top:1rem">Firmware: <span id="fw">–</span> · Gerät: <span id="dev">–</span></div>
<script>
const $=id=>document.getElementById(id);
const c=v=>v==null?"--,- °C":v.toFixed(1).replace(".",",")+" °C";
async function tick(){
  try{
    const r=await fetch("/data",{cache:"no-store"});
    if(!r.ok){$("age").textContent="Noch keine Daten vom Gerät empfangen.";return;}
    const {received_at,status:d}=await r.json();
    const ageS=Math.round((Date.now()-received_at)/1000);
    const a=$("age"); a.textContent="Letztes Update vor "+ageS+" s";
    a.className="age"+(ageS>120?" stale":"");
    if(ageS>120)a.textContent+=" — Gerät evtl. offline";
    $("demo").hidden=!d.mock;
    $("tk").textContent=c(d.T_kammer_mean);
    $("mode").textContent=d.mode||"–";
    $("rh").textContent=d.RH_kammer!=null?Math.round(d.RH_kammer)+"%":"–";
    $("comp").textContent=d.comp_running?"⬤ Läuft":"⬤ Aus";
    $("amps").textContent=(d.ss_current_a!=null?d.ss_current_a.toFixed(1):"–")+" A";
    $("evi").textContent=d.evi_open?"✅":"—";
    $("pv").textContent=(d.pv_w!=null?d.pv_w:"----")+" W";
    $("grid").textContent=d.grid_w!=null?(d.grid_w>=0?"↑ ":"↓ ")+Math.abs(d.grid_w)+" W":"–";
    $("day").textContent=d.pv_day_kwh!=null?d.pv_day_kwh.toFixed(1)+" kWh":"–";
    $("ww").textContent=c(d.T_ww_oben);$("pool").textContent=c(d.T_whirlpool);$("out").textContent=c(d.T_aussen);
    const al=$("alarms");
    if(d.alarm_flags){al.textContent="⚠ Aktiver Alarm (0x"+d.alarm_flags.toString(16)+")";al.className="alarm";}
    else{al.textContent="Keine aktiven Alarme";al.className="";}
    $("fw").textContent=d.fw||"–";$("dev").textContent=d.device||"–";
  }catch(e){$("age").textContent="Verbindungsfehler.";}
}
tick();setInterval(tick,5000);
</script></body></html>`;
