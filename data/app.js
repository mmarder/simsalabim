// Dashboard live client. Connects to /ws, renders SystemState, auto-reconnects.
// Marks the UI stale if no message arrives within the watchdog window.
(function () {
  "use strict";

  const MODE_LABELS = {
    NORMAL: "Normal", DEEP_COOL: "Tiefkühl", ECONOMY: "Sparmodus",
    BUFFER: "Puffer", HOTWATER: "Warmwasser", DEFROST: "Abtau",
    ALARM: "ALARM", STANDBY: "Standby"
  };

  let ws = null;
  let lastMsg = 0;
  let reconnectTimer = null;

  const $ = (id) => document.getElementById(id);

  function fmtC(v) {
    if (v === null || v === undefined) return "--,- °C";
    return v.toFixed(1).replace(".", ",") + " °C";
  }
  function fmtSecs(s) {
    if (s === null || s === undefined) return "--";
    const m = Math.floor(s / 60), sec = s % 60;
    return m > 0 ? `${m}m ${sec}s` : `${sec}s`;
  }
  function onoff(b) { return b ? "✅" : "—"; }

  function setLink(connected) {
    const el = $("link");
    el.textContent = connected ? "● LIVE" : "● GETRENNT";
    el.classList.toggle("off", !connected);
  }

  function render(d) {
    $("fw").textContent = d.fw || "?";
    $("demo").hidden = !d.mock;

    $("t_kammer").textContent = fmtC(d.T_kammer_mean);
    $("rh").textContent = d.RH_kammer != null ? Math.round(d.RH_kammer) + "%" : "--%";

    $("comp_state").textContent = d.comp_running ? "⬤ Läuft" : "⬤ Aus";
    $("comp_state").style.color = d.comp_running ? "var(--ok)" : "var(--muted)";
    $("ss_a").textContent = d.ss_current_a != null ? d.ss_current_a.toFixed(1) + " A" : "-,- A";
    $("comp_on").textContent = fmtSecs(d.comp_on_s);
    $("evi").textContent = onoff(d.evi_open);

    $("pv").textContent = d.pv_w != null ? d.pv_w + " W" : "---- W";
    const g = d.grid_w;
    $("grid").textContent = g != null
      ? (g >= 0 ? "↑ " + g + " W" : "↓ " + Math.abs(g) + " W")
      : "-- W";
    $("pv_day").textContent = d.pv_day_kwh != null ? d.pv_day_kwh.toFixed(1) + " kWh" : "-- kWh";
    $("mode").textContent = MODE_LABELS[d.mode] || d.mode || "---";

    $("ww_o").textContent = fmtC(d.T_ww_oben);
    $("ww_u").textContent = fmtC(d.T_ww_unten);
    $("pool").textContent = fmtC(d.T_whirlpool);
    $("bypass").textContent = onoff(d.bypass_open);

    $("t_kopf").textContent = fmtC(d.T_kopf);
    $("t_saug").textContent = fmtC(d.T_saug);
    $("t_aussen").textContent = fmtC(d.T_aussen);
    $("t_verd").textContent = fmtC(d.T_verd);

    $("ip").textContent = location.hostname;
    $("wifi").textContent = "verbunden";

    const alarmsEl = $("alarms");
    const card = $("alarm-card");
    if (d.alarm_flags && d.alarm_flags !== 0) {
      alarmsEl.textContent = "⚠ Aktiver Alarm (Code 0x" + d.alarm_flags.toString(16) + ")";
      alarmsEl.classList.add("active");
      card.classList.add("active");
    } else {
      alarmsEl.textContent = "Keine aktiven Alarme";
      alarmsEl.classList.remove("active");
      card.classList.remove("active");
    }

    document.body.classList.remove("stale");
  }

  function connect() {
    ws = new WebSocket("ws://" + location.host + "/ws");
    ws.onopen = () => setLink(true);
    ws.onmessage = (ev) => {
      lastMsg = Date.now();
      try { render(JSON.parse(ev.data)); } catch (e) { /* ignore bad frame */ }
    };
    ws.onclose = () => {
      setLink(false);
      if (!reconnectTimer) {
        reconnectTimer = setTimeout(() => { reconnectTimer = null; connect(); }, 2000);
      }
    };
    ws.onerror = () => { if (ws) ws.close(); };
  }

  // ── OTA status + controls ──────────────────────────────────────────────
  async function refreshOta() {
    try {
      const r = await fetch("/api/ota");
      const o = await r.json();
      $("ota_current").textContent = o.current || "?";
      $("ota_latest").textContent = o.latest || "(unbekannt)";
      const status = $("ota_status");
      const installBtn = $("ota_install");
      const fsWrap = $("ota_fs_wrap");
      if (o.installing) {
        status.textContent = "⏳ Update wird installiert… Gerät startet neu.";
        installBtn.hidden = true; fsWrap.hidden = true;
      } else if (o.available && o.auto) {
        status.textContent = "⚡ Auto-Update " + o.latest + " — wird automatisch installiert…";
        status.className = "ota-status avail";
        installBtn.hidden = true; fsWrap.hidden = true;
      } else if (o.available) {
        status.textContent = "⬆ Update verfügbar: " + o.latest;
        status.className = "ota-status avail";
        installBtn.hidden = false; fsWrap.hidden = false;
      } else if (o.latest) {
        status.textContent = "✅ Aktuell (neueste Version installiert)";
        status.className = "ota-status";
        installBtn.hidden = true; fsWrap.hidden = true;
      } else {
        status.textContent = "GitHub noch nicht erreicht — „GitHub prüfen“ klicken.";
        status.className = "ota-status";
        installBtn.hidden = true; fsWrap.hidden = true;
      }
    } catch (e) { /* device busy / offline */ }
  }

  document.addEventListener("DOMContentLoaded", () => {
    $("ota_check").addEventListener("click", async () => {
      $("ota_status").textContent = "Prüfe GitHub…";
      await fetch("/api/ota/check", { method: "POST" });
      setTimeout(refreshOta, 3000);  // give the background check time to run
    });
    $("ota_install").addEventListener("click", async () => {
      const withFs = $("ota_fs").checked;
      const msg = withFs
        ? "Update inkl. Weboberfläche installieren? Das Gerät startet neu."
        : "Update jetzt installieren? Das Gerät startet neu.";
      if (!confirm(msg)) return;
      $("ota_status").textContent = "⏳ Starte Update…";
      $("ota_install").hidden = true;
      await fetch("/api/ota/install" + (withFs ? "?fs=1" : ""), { method: "POST" });
    });
    refreshOta();
    setInterval(refreshOta, 10000);
  });

  // Clock + stale watchdog.
  setInterval(() => {
    const now = new Date();
    $("clock").textContent =
      String(now.getHours()).padStart(2, "0") + ":" +
      String(now.getMinutes()).padStart(2, "0");
    if (lastMsg && Date.now() - lastMsg > 5000) {
      document.body.classList.add("stale");
      setLink(false);
    }
  }, 1000);

  connect();
})();
