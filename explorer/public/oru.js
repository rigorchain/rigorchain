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
    return (
      `<p class="muted">${escapeHtml(snap.freshness || "")}</p>` +
      notes.join("") +
      `<p>${escapeHtml(data.explanation || "")}</p>` +
      '<div class="oru-sections">' +
      sectionHtml(sections.destruction) +
      sectionHtml(sections.res) +
      sectionHtml(sections.relationship) +
      "</div>"
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
