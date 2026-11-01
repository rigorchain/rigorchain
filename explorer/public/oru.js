/**
 * ORU Convergence page. One cumulative snapshot from /api/oru/economics.
 */
(function () {
  var REFRESH_MS = 60000;

  function escapeHtml(s) {
    return String(s)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function txPath(txid) {
    if (window.ExplorerRoutes && ExplorerRoutes.txPath) return ExplorerRoutes.txPath(txid);
    return "/tx/" + encodeURIComponent(txid);
  }

  function blockPath(hash) {
    if (window.ExplorerRoutes && ExplorerRoutes.blockPath) return ExplorerRoutes.blockPath(hash);
    return "/block/" + encodeURIComponent(hash);
  }

  function shortHash(hash) {
    const s = String(hash || "");
    if (!s) return "—";
    if (s.length <= 20) return s;
    return s.slice(0, 12) + "…" + s.slice(-6);
  }

  function rowsHtml(rows) {
    return (rows || [])
      .map(function (row) {
        const note = row.note ? `<div class="muted">${escapeHtml(row.note)}</div>` : "";
        return `<dt>${escapeHtml(row.label)}</dt><dd>${escapeHtml(row.value)}${note}</dd>`;
      })
      .join("");
  }

  function sectionHtml(section) {
    if (!section) return "";
    const groups = (section.groups || [])
      .map(function (group) {
        return `<h3>${escapeHtml(group.title)}</h3><dl>${rowsHtml(group.rows)}</dl>`;
      })
      .join("");
    const direct = section.rows && section.rows.length ? `<dl>${rowsHtml(section.rows)}</dl>` : "";
    return `<section class="status-card" id="${escapeHtml(section.id)}"><h2>${escapeHtml(section.title)}</h2>${direct}${groups}</section>`;
  }

  function renderOruPage(data) {
    const snap = data.snapshot || {};
    const sections = data.sections || {};
    const notes = [];
    if (snap.lag_note) notes.push(`<p class="muted">${escapeHtml(snap.lag_note)}</p>`);
    if (snap.accounting_note) notes.push(`<p class="muted">${escapeHtml(snap.accounting_note)}</p>`);
    const recent = data.recent || [];
    const body = recent
      .map(function (row) {
        return (
          "<tr><td>" +
          escapeHtml(row.height) +
          '</td><td><a class="mono" href="' +
          blockPath(row.block_hash) +
          '">' +
          escapeHtml(shortHash(row.block_hash)) +
          '</a></td><td><a class="mono" href="' +
          txPath(row.txid) +
          '">' +
          escapeHtml(shortHash(row.txid)) +
          "</a></td><td>" +
          escapeHtml(row.oru_destroyed_display) +
          "</td><td>" +
          escapeHtml(row.res_minted_display) +
          "</td></tr>"
        );
      })
      .join("");
    const history =
      '<h2>Recent confirmed Convergence transactions</h2>' +
      '<div class="history-wrap"><table><thead><tr><th>Height</th><th>Block</th><th>Transaction</th><th>ORU destroyed</th><th>RES minted</th></tr></thead><tbody>' +
      (body || '<tr><td colspan="5" class="muted">No confirmed Convergence transactions indexed.</td></tr>') +
      "</tbody></table></div>";
    return (
      `<p class="muted">${escapeHtml(snap.freshness || "")}</p>` +
      notes.join("") +
      `<p>${escapeHtml(data.explanation || "")}</p>` +
      '<div class="oru-sections">' +
      sectionHtml(sections.destruction) +
      sectionHtml(sections.res) +
      sectionHtml(sections.relationship) +
      "</div>" +
      history
    );
  }

  var inFlight = false;
  var refreshTimer = null;
  var refreshStarted = false;

  function documentHidden() {
    return typeof document !== "undefined" && document.visibilityState === "hidden";
  }

  async function load() {
    const root = document.getElementById("root");
    if (!root || inFlight) return;
    inFlight = true;
    try {
      const response = await (window.ExplorerRoutes && ExplorerRoutes.fetchJson
        ? ExplorerRoutes.fetchJson("/api/oru/economics")
        : fetch("/api/oru/economics", { cache: "no-store" }));
      if (!response.ok) {
        if (!root.querySelector(".status-card")) root.textContent = "Failed to load ORU statistics: HTTP " + response.status;
        return;
      }
      root.innerHTML = renderOruPage(await response.json());
    } catch (e) {
      if (!root.querySelector(".status-card")) root.textContent = "Failed: " + String(e);
    } finally {
      inFlight = false;
    }
  }

  function disarm() {
    if (refreshTimer != null) {
      clearInterval(refreshTimer);
      refreshTimer = null;
    }
  }

  function arm() {
    if (refreshTimer != null || refreshStarted !== true || documentHidden()) return;
    refreshTimer = setInterval(function () {
      if (documentHidden()) {
        disarm();
        return;
      }
      load().catch(function () {});
    }, REFRESH_MS);
  }

  function onVisibility() {
    if (!refreshStarted) return;
    if (documentHidden()) {
      disarm();
      return;
    }
    load().catch(function () {});
    arm();
  }

  function start() {
    if (refreshStarted) return;
    refreshStarted = true;
    document.addEventListener("visibilitychange", onVisibility);
    window.addEventListener("pagehide", disarm);
    load().catch(function () {});
    arm();
  }

  function stop() {
    disarm();
    if (!refreshStarted) return;
    refreshStarted = false;
    document.removeEventListener("visibilitychange", onVisibility);
    window.removeEventListener("pagehide", disarm);
  }

  window.OruPage = {
    REFRESH_MS: REFRESH_MS,
    renderOruPage: renderOruPage,
    load: load,
    start: start,
    stop: stop,
  };
})();
