/**
 * Canonical Explorer routing helpers.
 * Read-only UI; JSON data via /api/*.
 */
(function () {
  function segmentAfter(prefix) {
    const p = window.location.pathname;
    if (!p.startsWith(prefix)) return "";
    const rest = p.slice(prefix.length).replace(/\/$/, "");
    return rest ? decodeURIComponent(rest) : "";
  }

  function installCanonicalLink() {
    const href = window.location.origin + window.location.pathname;
    let link = document.querySelector('link[rel="canonical"]');
    if (!link) {
      link = document.createElement("link");
      link.rel = "canonical";
      document.head.appendChild(link);
    }
    link.href = href;
  }

  function navHtml() {
    return `
      <a href="/">Home</a>
      <a href="/blocks">Blocks</a>
      <a href="/oru.html">ORU</a>
      <a href="/res.html">RES</a>
      <a href="/res-events.html">RES events</a>
      <a href="/address">Address</a>
      <a href="/search">Search</a>
    `;
  }

  function mountNav(containerId) {
    const el = document.getElementById(containerId);
    if (!el) return;
    el.classList.add("site-nav");
    el.classList.remove("muted", "row");
    el.innerHTML = navHtml();
  }

  function relativePhrase(n, unit, future) {
    const body = n === 1 ? `1 ${unit}` : `${n} ${unit}s`;
    return future ? `in ${body}` : `${body} ago`;
  }

  function timeAgo(unixSeconds, nowSeconds) {
    const t = Number(unixSeconds);
    if (!Number.isFinite(t) || t <= 0) return "—";
    const now =
      arguments.length > 1 && Number.isFinite(Number(nowSeconds))
        ? Number(nowSeconds)
        : Date.now() / 1000;
    const signed = Math.floor(now - t);
    if (!Number.isFinite(signed)) return "—";
    if (signed < 0) {
      const ahead = -signed;
      if (ahead < 60) return relativePhrase(ahead, "second", true);
      const minutes = Math.floor(ahead / 60);
      if (minutes < 60) return relativePhrase(minutes, "minute", true);
      const hours = Math.floor(minutes / 60);
      if (hours < 24) return relativePhrase(hours, "hour", true);
      const days = Math.floor(hours / 24);
      if (days < 30) return relativePhrase(days, "day", true);
      const months = Math.floor(days / 30);
      if (months < 12) return relativePhrase(months, "month", true);
      return relativePhrase(Math.floor(days / 365), "year", true);
    }
    const diff = signed;
    if (diff < 5) return "just now";
    if (diff < 60) return relativePhrase(diff, "second", false);
    const minutes = Math.floor(diff / 60);
    if (minutes < 60) return relativePhrase(minutes, "minute", false);
    const hours = Math.floor(minutes / 60);
    if (hours < 24) return relativePhrase(hours, "hour", false);
    const days = Math.floor(hours / 24);
    if (days < 30) return relativePhrase(days, "day", false);
    const months = Math.floor(days / 30);
    if (months < 12) return relativePhrase(months, "month", false);
    return relativePhrase(Math.floor(days / 365), "year", false);
  }

  function timeAbsolute(unixSeconds) {
    const t = Number(unixSeconds);
    if (!Number.isFinite(t) || t <= 0) return "";
    const whole = Math.trunc(t);
    const iso = new Date(whole * 1000).toISOString();
    return iso.slice(0, 19).replace("T", " ") + " UTC";
  }

  function blockTimeDetailHtml(unixSeconds, nowSeconds) {
    const abs = timeAbsolute(unixSeconds);
    if (!abs) return "—";
    const rel = timeAgo(unixSeconds, nowSeconds);
    return abs + '<div class="muted">' + rel + "</div>";
  }

  function fetchJson(url, timeoutMs) {
    const ms = timeoutMs == null ? 15000 : timeoutMs;
    const ctrl = new AbortController();
    const timer = setTimeout(function () {
      ctrl.abort();
    }, ms);
    return fetch(url, { signal: ctrl.signal, cache: "no-store" }).finally(function () {
      clearTimeout(timer);
    });
  }

  function timeCellHtml(unixSeconds, nowSeconds) {
    const abs = timeAbsolute(unixSeconds);
    if (!abs) return "—";
    const title = abs.replace(/&/g, "&amp;").replace(/"/g, "&quot;");
    const rel = arguments.length > 1 ? timeAgo(unixSeconds, nowSeconds) : timeAgo(unixSeconds);
    return `<span title="${title}">${rel}</span>`;
  }

  window.ExplorerRoutes = {
    blockHash: () => segmentAfter("/block/"),
    txid: () => segmentAfter("/tx/"),
    address: () => segmentAfter("/address/"),
    txPath: (txid) => "/tx/" + encodeURIComponent(txid),
    blockPath: (hash) => "/block/" + encodeURIComponent(hash),
    addressPath: (addr) => "/address/" + encodeURIComponent(addr),
    installCanonicalLink,
    mountNav,
    timeAgo,
    timeAbsolute,
    blockTimeDetailHtml,
    timeCellHtml,
    fetchJson,
    apiBase: "/api",
  };
})();
