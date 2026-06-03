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

    if (url.pathname === "/data" && request.method === "GET") {
      const raw = await env.STATUS_KV.get(KV_KEY);
      if (!raw) return json({ error: "no data yet" }, 404);
      return new Response(raw, {
        headers: { "content-type": "application/json", "cache-control": "no-store" },
      });
    }

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
  :root{--bg:#0d1117;--card:#161b22;--fg:#e6edf3;--muted:#8b949e;--accent:#58a6ff;--cold:#6cb6ff;--ok:#3fb950;--warn:#f0883e;--alarm:#f85149}
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:system-ui,sans-serif;background:var(--bg);color:var(--fg);padding:1rem;max-width:820px;margin:0 auto}
  h1{font-size:1.1rem}
  .age{font-size:.85rem;color:var(--muted);margin-top:.2rem}
  .age.stale{color:var(--alarm);font-weight:600}
  /* big demo / live banner */
  .banner{margin:.8rem 0;padding:.7rem .9rem;border-radius:10px;font-weight:600;font-size:.9rem}
  .banner.demo{background:#3a2a0f;border:1px solid var(--warn);color:var(--warn)}
  .banner.live{background:#0f2a16;border:1px solid var(--ok);color:var(--ok)}
  .sec-h{display:flex;align-items:center;gap:.5rem;margin:1.2rem 0 .5rem;font-size:.8rem;letter-spacing:.06em;color:var(--muted)}
  .tag{font-size:.65rem;padding:.12rem .5rem;border-radius:999px;font-weight:700}
  .tag.demo{background:var(--warn);color:#1a1207}
  .tag.live{background:var(--ok);color:#04130a}
  .grid{display:grid;grid-template-columns:repeat(2,1fr);gap:.7rem}
  .grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:.7rem}
  .card{background:var(--card);border:1px solid #30363d;border-radius:12px;padding:.8rem}
  .card.sim{border-style:dashed;border-color:#5a4422}
  .card h2{font-size:.68rem;letter-spacing:.06em;color:var(--muted);margin:0 0 .4rem}
  .big{font-size:1.5rem;font-weight:700;color:var(--cold);font-variant-numeric:tabular-nums}
  .row{font-size:.84rem;margin-top:.22rem}
  .row b{color:var(--accent);font-variant-numeric:tabular-nums}
  .alarm{color:var(--alarm);font-weight:600}
  .okc{color:var(--ok)}
  footer{margin-top:1.4rem;font-size:.78rem;color:var(--muted)}
</style></head><body>
<h1>❄ Kältekammer Samosir — Online-Status</h1>
<div class="age" id="age">lädt…</div>
<div id="banner" class="banner"></div>

<div class="sec-h">BETRIEBSDATEN (Sensorik) <span id="sec_sensor_tag" class="tag"></span></div>
<div class="grid">
  <div class="card sim" id="c_kammer"><h2>KAMMER</h2><div class="big" id="tk">--,- °C</div>
    <div class="row" id="tk3">oben/mitte/unten: –</div><div class="row">Modus: <b id="mode">–</b></div>
    <div class="row">RH: <b id="rh">–</b></div></div>
  <div class="card sim" id="c_comp"><h2>KOMPRESSOR</h2><div class="big" id="comp">–</div>
    <div class="row">Strom: <b id="amps">– A</b></div><div class="row">Laufzeit: <b id="con">–</b></div>
    <div class="row">EVI: <b id="evi">–</b></div></div>
  <div class="card sim" id="c_circ"><h2>KÄLTEKREIS</h2>
    <div class="row">Druckgas T4: <b id="tkopf">–</b></div><div class="row">Sauggas T5: <b id="tsaug">–</b></div>
    <div class="row">Verdampfer T9: <b id="tverd">–</b></div><div class="row">Überhitzung: <b id="sh">–</b></div></div>
  <div class="card sim" id="c_pv"><h2>SOLAR PV</h2><div class="big" id="pv">---- W</div>
    <div class="row">Grid: <b id="grid">–</b></div><div class="row">Heute: <b id="day">–</b></div></div>
  <div class="card sim" id="c_heat"><h2>WÄRME</h2>
    <div class="row">WW oben/unten: <b id="ww">–</b></div><div class="row">Pool: <b id="pool">–</b></div>
    <div class="row">BPHE: <b id="bphe">–</b></div><div class="row">Außen: <b id="out">–</b></div></div>
  <div class="card sim" id="c_io"><h2>AKTOREN / EINGÄNGE</h2>
    <div class="row">Blower <b id="blow">–</b> · Defrost <b id="defr">–</b> · Bypass <b id="byp">–</b></div>
    <div class="row">Tür: <b id="door">–</b></div>
    <div class="row">HD-Wächter <b id="hp">–</b> · ND-Wächter <b id="lp">–</b></div>
    <div class="row">Modbus PV <b id="pvv">–</b> · SS <b id="ssv">–</b></div></div>
</div>

<div class="sec-h">ALARME <span id="sec_sensor_tag2" class="tag"></span></div>
<div class="card sim"><div id="alarms">–</div></div>

<div class="sec-h">GERÄTE-DIAGNOSE <span class="tag live">LIVE / ECHT</span></div>
<div class="grid3">
  <div class="card"><h2>FIRMWARE</h2><div class="big" id="fw" style="font-size:1.2rem">–</div>
    <div class="row">Gerät: <b id="dev">–</b></div></div>
  <div class="card"><h2>LAUFZEIT</h2><div class="big" id="uptime" style="font-size:1.2rem">–</div>
    <div class="row">Reset: <b id="reset">–</b></div></div>
  <div class="card"><h2>SYSTEM</h2><div class="row">Free heap: <b id="heap">–</b></div>
    <div class="row">WiFi RSSI: <b id="rssi">–</b></div>
    <div class="row">Update vor: <b id="postage">–</b></div></div>
</div>

<footer>Auto-Refresh alle 5 s · <span id="src">–</span></footer>
<script>
const $=id=>document.getElementById(id);
const c=v=>v==null?"–":v.toFixed(1).replace(".",",")+" °C";
const onoff=b=>b?'<span class="okc">EIN</span>':'AUS';
const okbad=(b,g,r)=>b?'<span class="okc">'+(g||"OK")+'</span>':'<span class="alarm">'+(r||"!")+'</span>';
// alarm bit -> code/name (ARCHITECTURE §7.1 / §9.4)
const ALARMS=["A01 Druckgas>115°C","A02 Hochdruckwächter","A03 Niederdruckwächter",
"A04 SS Phasenfolge","A05 SS Phasenausfall","A06 SS Überstrom","A07 SS Überlast",
"A08 SS Stromunsymmetrie","A09 SS Übertemp.","A10 Kammer>-15°C","A11 Sensor-Ausfall",
"A12 Tür>2min","A13 WW>70°C","A14 Deye RS485","A15 SS RS485","A16 Druckgas>100°C"];
function fmtUptime(s){if(s==null)return"–";const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+"d ":"")+h+"h "+m+"m";}
function rssiTxt(r){if(r==null||r===0)return"–";let q=r>=-60?"gut":r>=-70?"ok":r>=-80?"schwach":"sehr schwach";return r+" dBm ("+q+")";}

async function tick(){
  try{
    const r=await fetch("/data",{cache:"no-store"});
    if(!r.ok){$("age").textContent="Noch keine Daten vom Gerät empfangen.";return;}
    const {received_at,status:d}=await r.json();
    const ageS=Math.round((Date.now()-received_at)/1000);
    const a=$("age");a.textContent="Letztes Update vor "+ageS+" s";a.className="age"+(ageS>120?" stale":"");
    if(ageS>120)a.textContent+=" — Gerät evtl. offline";

    // demo vs live banner + section tags
    const demo=!!d.mock;
    const b=$("banner");
    if(demo){b.className="banner demo";b.innerHTML="⚠ DEMO-MODUS — die <b>Sensorwerte sind simuliert</b> (Phase 0, noch keine echten Fühler). Die <b>Geräte-Diagnose</b> unten ist echt.";}
    else{b.className="banner live";b.textContent="● LIVE — echte Sensordaten.";}
    const tag=demo?'<span class="tag demo">SIMULIERT</span>':'<span class="tag live">ECHT</span>';
    $("sec_sensor_tag").innerHTML=tag;$("sec_sensor_tag2").innerHTML=tag;
    document.querySelectorAll('.card.sim').forEach(c=>c.style.borderStyle=demo?"dashed":"solid");

    // sensors
    $("tk").textContent=c(d.T_kammer_mean);
    if(Array.isArray(d.T_kammer))$("tk3").textContent="oben/mitte/unten: "+d.T_kammer.map(x=>x==null?"–":x.toFixed(1)).join(" / ")+" °C";
    $("mode").textContent=d.mode||"–";
    $("rh").textContent=d.RH_kammer!=null?Math.round(d.RH_kammer)+"%":"–";
    $("comp").innerHTML=d.comp_running?'<span class="okc">⬤ Läuft</span>':"⬤ Aus";
    $("amps").textContent=(d.ss_current_a!=null?d.ss_current_a.toFixed(1):"–")+" A";
    $("con").textContent=d.comp_on_s!=null?fmtUptime(d.comp_on_s):"–";
    $("evi").innerHTML=onoff(d.evi_open);
    $("tkopf").textContent=c(d.T_kopf);$("tsaug").textContent=c(d.T_saug);$("tverd").textContent=c(d.T_verd);
    $("sh").textContent=(d.T_saug!=null&&d.T_verd!=null)?(d.T_saug-d.T_verd).toFixed(1).replace(".",",")+" K":"–";
    $("pv").textContent=(d.pv_w!=null?d.pv_w:"----")+" W";
    $("grid").textContent=d.grid_w!=null?(d.grid_w>=0?"↑ ":"↓ ")+Math.abs(d.grid_w)+" W":"–";
    $("day").textContent=d.pv_day_kwh!=null?d.pv_day_kwh.toFixed(1)+" kWh":"–";
    $("ww").textContent=c(d.T_ww_oben)+" / "+c(d.T_ww_unten);
    $("pool").textContent=c(d.T_whirlpool);$("bphe").textContent=c(d.T_bphe);$("out").textContent=c(d.T_aussen);
    $("blow").innerHTML=onoff(d.blower_on);$("defr").innerHTML=onoff(d.defrost_on);$("byp").innerHTML=onoff(d.bypass_open);
    $("door").innerHTML=d.door_open?'<span class="alarm">OFFEN</span>':'<span class="okc">zu</span>';
    $("hp").innerHTML=okbad(d.hp_ok,"OK","offen!");$("lp").innerHTML=okbad(d.lp_ok,"OK","offen!");
    $("pvv").innerHTML=okbad(d.pv_valid,"OK","Fehler");$("ssv").innerHTML=okbad(d.ss_valid,"OK","Fehler");

    // alarms decoded
    const al=$("alarms");const f=d.alarm_flags|0;
    if(f){const names=[];for(let i=0;i<16;i++)if(f&(1<<i))names.push(ALARMS[i]);
      al.className="alarm";al.innerHTML="⚠ "+names.join("<br>⚠ ");}
    else{al.className="okc";al.textContent="Keine aktiven Alarme";}

    // device health (real)
    $("fw").textContent=d.fw||"–";$("dev").textContent=d.device||"–";
    $("uptime").textContent=fmtUptime(d.uptime_s);
    $("reset").textContent=d.reset_reason||"–";
    $("heap").textContent=d.free_heap!=null?Math.round(d.free_heap/1024)+" KB":"–";
    $("rssi").textContent=rssiTxt(d.rssi);
    $("postage").textContent=ageS+" s";
    $("src").textContent=(demo?"DEMO-Daten":"Live-Daten")+" · fw "+(d.fw||"?");
  }catch(e){$("age").textContent="Verbindungsfehler.";}
}
tick();setInterval(tick,5000);
</script></body></html>`;
