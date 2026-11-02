/**
 * Chain status dashboard. Read-only node and indexer snapshot.
 */
(function () {
  function escapeHtml(s) {
    return String(s)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function fmt(v) {
    if (v === null || v === undefined || v === "") return "—";
    return escapeHtml(v);
  }

  function snapshotText(data, value) {
    if (value !== null && value !== undefined && value !== "") return fmt(value);
    var cache = data && data.cache;
    if (cache && cache.economic_status === "loading") return "Loading…";
    if (cache && cache.economic_status === "refresh_failed") return "Unavailable";
    return fmt(value);
  }

  function snapshotAge(data) {
    var cache = data && data.cache;
    if (!cache || cache.economic_generated_at == null) return "";
    if (cache.economic_status === "loading") return "Loading…";
    if (cache.economic_status === "refresh_failed" && cache.cache_age_seconds == null) return "Unavailable";
    var age = cache.cache_age_seconds;
    var label = age == null ? "cached" : "cached " + age + "s";
    if (cache.stale) label += ", updating";
    return label;
  }

  function dlRow(label, value) {
    return `<dt>${escapeHtml(label)}</dt><dd>${value}</dd>`;
  }

  var HASHRATE_LABEL = "Network Hashrate (50 blocks)";
  var HASHRATE_NOTE =
    "Estimated from actual block proof-of-work over the latest 50 block intervals. This is not a direct measurement of miner hardware.";
  var HOME_REFRESH_MS = 60000;

  function hashrateCell(bh) {
    var row = bh && bh.network_hashrate && typeof bh.network_hashrate === "object" ? bh.network_hashrate : {};
    var display = typeof row.display === "string" ? row.display : "";
    var usable = row.available === true && display !== "" && !/^0(?:\.0+)?(?:\s|$)/.test(display);
    var text = usable ? display : "Unavailable";
    var note = typeof row.note === "string" && row.note ? row.note : HASHRATE_NOTE;
    return `<span title="${escapeHtml(note)}">${escapeHtml(text)}</span>`;
  }

  function shortMono(h) {
    const s = String(h ?? "");
    if (!s) return "—";
    const label = s.length > 20 ? s.slice(0, 12) + "…" + s.slice(-6) : s;
    return `<span class="status-mono" title="${escapeHtml(s)}">${escapeHtml(label)}</span>`;
  }

  function renderSection(title, rows) {
    return `<section class="status-card"><h2>${escapeHtml(title)}</h2><dl>${rows.join("")}</dl></section>`;
  }

  function networkLabel(id) {
    const n = String((id && (id.network || id.network_type)) || "").trim();
    return n ? n.toUpperCase() : "—";
  }

  function renderDashboard(data) {
    const id = data.chain_identity ?? {};
    const bh = data.block_health ?? {};
    const eco = data.economic_snapshot ?? {};
    const mine = data.mining_activity ?? {};
    const sys = data.system_integrity ?? {};
    const comp = sys.explorer_compliance ?? {};
    const issued =
      eco.supply_chart && eco.supply_chart.total_issued_oru
        ? eco.supply_chart.total_issued_oru.display
        : eco.oru_supply_cumulative_display;
    const net = networkLabel(id);

    const sections = [
      renderSection("Chain identity", [
        dlRow("Chain", fmt(id.chain_name)),
        dlRow("Network", fmt(net)),
        dlRow("Genesis", shortMono(id.genesis_hash)),
        dlRow("Node", fmt(id.product ?? id.node_subversion ?? id.node_version)),
        dlRow("Subversion", fmt(id.node_subversion)),
        dlRow("Best block", shortMono(bh.best_block_hash)),
        dlRow(HASHRATE_LABEL, hashrateCell(bh)),
      ]),
      renderSection("Block production", [
        dlRow("Height (node)", fmt(bh.current_height_node)),
        dlRow("Avg block time (100)", bh.avg_block_time_seconds_last_100 != null ? `${Number(bh.avg_block_time_seconds_last_100).toFixed(1)} s` : "—"),
        dlRow("Difficulty", fmt(bh.difficulty)),
        dlRow("nBits", fmt(bh.bits)),
        dlRow("OpuHash", shortMono(bh.opus_hash ?? bh.pow_hash)),
        dlRow("Peers", fmt(mine.connections)),
        dlRow("Mempool tx count", fmt(mine.mempool_tx_count)),
      ]),
      renderSection("Indexer integrity", [
        dlRow("Height (indexer)", fmt(bh.current_height_indexer)),
        dlRow("Indexer lag (blocks)", fmt(sys.indexer_sync_lag_blocks)),
        dlRow("Indexer lag (seconds)", fmt(sys.indexer_sync_lag_seconds)),
        dlRow("Mismatch active", comp.mismatch_active ? "yes" : "no"),
        dlRow("Reorg signal", `${fmt(bh.reorg_signal_blocks_mismatch_last_1000)} mismatch blocks / 1000`),
        dlRow("IBD", fmt(bh.initialblockdownload)),
      ].concat(sys.rpc_error ? [dlRow("RPC", fmt(sys.rpc_error))] : [])),
      renderSection("Issuance", [
        dlRow("Issued ORU", fmt(issued)),
        dlRow("RES minted", snapshotText(data, eco.network_res_minted_display)),
        dlRow("Convergence destroyed ORU", fmt(eco.cumulative_convergence_display ?? eco.oru_burned_lifetime_display)),
        dlRow("ORU destroyed to mint 1 RES", fmt(eco.res_destroy_cost_display)),
        dlRow("Confirming height", fmt(eco.confirm_height ?? eco.tip_height)),
        dlRow("ORU subsidy", fmt(eco.oru_subsidy_tip_display)),
        dlRow("Last mint height", fmt(eco.last_res_height)),
      ].concat(snapshotAge(data) ? [dlRow("Snapshot", snapshotAge(data))] : [])),
    ];

    return `
      <p class="network-mark">NETWORK <strong>${escapeHtml(net)}</strong></p>
      <div class="status-grid status-grid--pair">${sections.join("")}</div>
    `;
  }

  var lastHtml = "";
  var inFlight = false;
  var refreshTimer = null;
  var refreshStarted = false;
  var loadingRetry = null;

  function documentHidden() {
    return typeof document !== "undefined" && document.visibilityState === "hidden";
  }

  function rememberSsr(root) {
    if (root && root.getAttribute && root.getAttribute("data-ssr") === "1" && !lastHtml) {
      lastHtml = root.innerHTML;
    }
  }

  function applyDashboard(root, html) {
    if (html === lastHtml && root.querySelector && root.querySelector(".status-grid")) return;
    root.innerHTML = html;
    lastHtml = html;
  }

  async function load(opts) {
    const preserve = !!(opts && opts.preserve);
    const root = document.getElementById("status-root");
    if (!root) return;
    rememberSsr(root);
    if (inFlight) return;
    inFlight = true;
    try {
      const hasDashboard = root.querySelector && root.querySelector(".status-grid");
      if (!preserve && root.getAttribute("data-ssr") !== "1" && !hasDashboard) {
        root.textContent = "Loading…";
        lastHtml = "";
      }
      const r = await (window.ExplorerRoutes && ExplorerRoutes.fetchJson
        ? ExplorerRoutes.fetchJson("/api/status")
        : fetch("/api/status", { cache: "no-store" }));
      if (!r.ok) {
        if (!preserve && !(root.querySelector && root.querySelector(".status-grid"))) {
          root.textContent = "Failed to load status: HTTP " + r.status;
          lastHtml = "";
        }
        return;
      }
      const data = await r.json();
      applyDashboard(root, renderDashboard(data));
      scheduleSnapshotRetry(data);
    } finally {
      inFlight = false;
    }
  }

  function scheduleSnapshotRetry(data) {
    var cache = data && data.cache;
    var waiting = cache && (cache.economic_status === "loading" || cache.live_status === "loading");
    if (!waiting) {
      if (loadingRetry != null) {
        clearTimeout(loadingRetry);
        loadingRetry = null;
      }
      return;
    }
    if (loadingRetry != null) return;
    loadingRetry = setTimeout(function () {
      loadingRetry = null;
      load({ preserve: true }).catch(function () {});
    }, 2000);
  }

  function disarmRefresh() {
    if (refreshTimer != null) {
      clearInterval(refreshTimer);
      refreshTimer = null;
    }
  }

  function armRefresh() {
    if (refreshTimer != null || refreshStarted !== true || documentHidden()) return;
    refreshTimer = setInterval(function () {
      if (documentHidden()) {
        disarmRefresh();
        return;
      }
      load({ preserve: true }).catch(function () {});
    }, HOME_REFRESH_MS);
  }

  function onVisibility() {
    if (!refreshStarted) return;
    if (documentHidden()) {
      disarmRefresh();
      return;
    }
    load({ preserve: true }).catch(function () {});
    armRefresh();
  }

  function onPageHide() {
    disarmRefresh();
  }

  function onPageShow(event) {
    if (!refreshStarted || documentHidden()) return;
    if (event && event.persisted) {
      load({ preserve: true }).catch(function () {});
    }
    armRefresh();
  }

  function startAutoRefresh() {
    if (refreshStarted) return;
    refreshStarted = true;
    const root = document.getElementById("status-root");
    rememberSsr(root);
    document.addEventListener("visibilitychange", onVisibility);
    window.addEventListener("pagehide", onPageHide);
    window.addEventListener("pageshow", onPageShow);
    armRefresh();
  }

  function stopAutoRefresh() {
    disarmRefresh();
    if (loadingRetry != null) {
      clearTimeout(loadingRetry);
      loadingRetry = null;
    }
    if (!refreshStarted) return;
    refreshStarted = false;
    document.removeEventListener("visibilitychange", onVisibility);
    window.removeEventListener("pagehide", onPageHide);
    window.removeEventListener("pageshow", onPageShow);
  }

  window.StatusDashboard = {
    load: load,
    renderDashboard: renderDashboard,
    startAutoRefresh: startAutoRefresh,
    stopAutoRefresh: stopAutoRefresh,
    HOME_REFRESH_MS: HOME_REFRESH_MS,
  };
})();
