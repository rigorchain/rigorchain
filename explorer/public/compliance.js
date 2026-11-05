/**
 * RIGOR Explorer compliance layer.
 * Read-only display: footer and global mismatch banner.
 * Polling pauses while the document is hidden.
 */
(function () {
  "use strict";

  var POLL_MS = 30000;
  var pollTimer = null;
  var started = false;

  function esc(s) {
    return String(s ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function ensureShell() {
    var banner = document.getElementById("rigor-compliance-banner");
    if (!banner) {
      banner = document.createElement("div");
      banner.id = "rigor-compliance-banner";
      banner.setAttribute("role", "alert");
      banner.setAttribute("aria-live", "assertive");
      document.body.insertBefore(banner, document.body.firstChild);
    }

    var leftoverHeader = document.getElementById("rigor-compliance-header");
    if (leftoverHeader && leftoverHeader.parentNode) {
      leftoverHeader.parentNode.removeChild(leftoverHeader);
    }

    var footer = document.getElementById("rigor-compliance-footer");
    if (!footer) {
      footer = document.createElement("footer");
      footer.id = "rigor-compliance-footer";
      document.body.appendChild(footer);
    }

    return { banner: banner, footer: footer };
  }

  function renderFooter(footerEl, data) {
    footerEl.innerHTML =
      '<div class="rigor-footer-meta">' +
      "<span>Chain: " +
      esc(data.chain_name) +
      "</span>" +
      "<span>Network: " +
      esc(String(data.network_type || "").toLowerCase() === "mainnet" ? "MAINNET" : (data.network_type || "")) +
      "</span>" +
      "<span>Genesis: " +
      esc(data.genesis_hash_short || "—") +
      "</span>" +
      "<span>Node: " +
      esc(data.product) +
      "</span>" +
      "<span>Build: " +
      esc(data.build_timestamp) +
      "</span>" +
      "</div>";
  }

  function renderBanner(bannerEl, data) {
    if (data.mismatch_active) {
      var items = (data.mismatch_messages || [])
        .map(function (msg) {
          return "<li>" + esc(msg) + "</li>";
        })
        .join("");
      bannerEl.innerHTML =
        "<strong>CRITICAL: Explorer mismatch detected — do not trust indexed data until resolved.</strong>" +
        "<ul>" +
        items +
        "</ul>" +
        "<span>Verify via your own node RPC. This banner cannot be dismissed.</span>";
      bannerEl.classList.add("rigor-compliance-banner--active");
      bannerEl.classList.remove("rigor-compliance-banner--warn");
      document.body.classList.add("rigor-compliance-has-banner");
      return;
    }

    if (data.rpc_degraded) {
      var rpcItems = (data.rpc_messages || [])
        .map(function (msg) {
          return "<li>" + esc(msg) + "</li>";
        })
        .join("");
      bannerEl.innerHTML =
        "<strong>Node RPC busy — compliance checks skipped (not a chain mismatch).</strong>" +
        "<ul>" +
        rpcItems +
        "</ul>" +
        "<span>Work queue full is load on rigord. Indexed pages are not automatically untrusted. Retry shortly.</span>";
      bannerEl.classList.add("rigor-compliance-banner--active");
      bannerEl.classList.add("rigor-compliance-banner--warn");
      document.body.classList.add("rigor-compliance-has-banner");
      return;
    }

    if (data.catchup_active) {
      var catchupItems = (data.catchup_messages || [])
        .map(function (msg) {
          return "<li>" + esc(msg) + "</li>";
        })
        .join("");
      bannerEl.innerHTML =
        "<strong>Indexer catching up — not a chain fork.</strong>" +
        "<ul>" +
        catchupItems +
        "</ul>" +
        "<span>Tip hash at the indexed height still matches the node. Refresh; this banner clears when the indexer reaches the tip.</span>";
      bannerEl.classList.add("rigor-compliance-banner--active");
      bannerEl.classList.add("rigor-compliance-banner--warn");
      document.body.classList.add("rigor-compliance-has-banner");
      return;
    }

    bannerEl.classList.remove("rigor-compliance-banner--active");
    bannerEl.classList.remove("rigor-compliance-banner--warn");
    bannerEl.innerHTML = "";
    document.body.classList.remove("rigor-compliance-has-banner");
  }

  function applyCompliance(data) {
    var shell = ensureShell();
    renderFooter(shell.footer, data);
    renderBanner(shell.banner, data);
  }

  function fetchCompliance() {
    return fetch("/compliance", { cache: "no-store", signal: AbortSignal.timeout(15000) }).then(function (r) {
      if (!r.ok) throw new Error("compliance fetch failed: " + r.status);
      return r.json();
    });
  }

  function refresh() {
    fetchCompliance()
      .then(applyCompliance)
      .catch(function (e) {
        var shell = ensureShell();
        renderBanner(shell.banner, {
          mismatch_active: false,
          rpc_degraded: true,
          rpc_messages: ["Compliance endpoint unreachable: " + String(e)],
        });
      });
  }

  function hidden() {
    return document.visibilityState === "hidden";
  }

  function disarm() {
    if (pollTimer) {
      window.clearInterval(pollTimer);
      pollTimer = null;
    }
  }

  function arm() {
    if (pollTimer || !started || hidden()) return;
    pollTimer = window.setInterval(refresh, POLL_MS);
  }

  function onVisibility() {
    if (!started) return;
    if (hidden()) {
      disarm();
      return;
    }
    refresh();
    arm();
  }

  function onPageHide() {
    disarm();
  }

  function onPageShow(event) {
    if (!started || hidden()) return;
    if (event && event.persisted) refresh();
    arm();
  }

  function start() {
    if (started) return;
    started = true;
    document.addEventListener("visibilitychange", onVisibility);
    window.addEventListener("pagehide", onPageHide);
    window.addEventListener("pageshow", onPageShow);
    refresh();
    arm();
  }

  function stop() {
    disarm();
    if (!started) return;
    started = false;
    document.removeEventListener("visibilitychange", onVisibility);
    window.removeEventListener("pagehide", onPageHide);
    window.removeEventListener("pageshow", onPageShow);
  }

  window.CompliancePoll = { start: start, stop: stop, POLL_MS: POLL_MS };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", start);
  } else {
    start();
  }
})();
