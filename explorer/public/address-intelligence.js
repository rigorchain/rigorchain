/**
 * Address intelligence page. Read-only indexed data.
 */
(function () {
  function escapeHtml(s) {
    return String(s)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function copyBtn(text) {
    return `<button type="button" class="convergence-copy-btn" data-copy="${escapeHtml(text)}">Copy</button>`;
  }

  function dl(rows) {
    return `<dl>${rows.map(([k, v]) => `<dt>${escapeHtml(k)}</dt><dd>${v}</dd>`).join("")}</dl>`;
  }

  function card(title, body) {
    return `<section class="address-card"><h2>${escapeHtml(title)}</h2>${body}</section>`;
  }

  function renderUtxoSummary(label, summary, tagClass) {
    if (!summary) return "";
    return `<div class="address-balance-box ${tagClass}"><strong class="${tagClass === "oru" ? "address-tag-oru" : "address-tag-res"}">${escapeHtml(label)}</strong>${dl([
      ["Total UTXOs", String(summary.total_utxo_count ?? 0)],
      ["Spendable UTXOs", String(summary.spendable_utxo_count ?? 0)],
      ["Total received", escapeHtml(summary.total_received ?? "—")],
      ["Total spent", escapeHtml(summary.total_spent ?? "—")],
      ["Current balance", escapeHtml(summary.current_balance ?? "—")],
    ])}</div>`;
  }

  function renderHistory(history, routes) {
    const items = history?.items ?? [];
    if (items.length === 0) return "<p class=\"muted\">No indexed transaction history for this address.</p>";
    const txPath = routes.txPath.bind(routes);
    const rows = items
      .map(
        (row) => `<tr>
        <td><a href="${escapeHtml(txPath(row.txid))}" class="address-mono">${escapeHtml(String(row.txid).slice(0, 16))}…</a></td>
        <td>${row.block_height ?? "—"}</td>
        <td>${row.confirmations ?? "—"}</td>
        <td class="${row.asset_type === "RES" ? "address-tag-res" : "address-tag-oru"}">${escapeHtml(row.asset_type)}</td>
        <td>${escapeHtml(row.received_amount ?? "0")}</td>
        <td>${escapeHtml(row.sent_amount ?? "0")}</td>
        <td>${row.asset_type === "RES" ? (row.counterparty_address ? `<a href="${escapeHtml(routes.addressPath(row.counterparty_address))}">${escapeHtml(row.counterparty_address)}</a>` : escapeHtml(row.counterparty_label || "Unavailable")) : "—"}</td>
        <td>${row.timestamp ? new Date(row.timestamp * 1000).toISOString() : "—"}</td>
      </tr>`,
      )
      .join("");
    return `<div class="address-table-wrap"><table class="address-table">
      <thead><tr><th>txid</th><th>height</th><th>conf</th><th>asset</th><th>received</th><th>sent</th><th>counterparty</th><th>time</th></tr></thead>
      <tbody>${rows}</tbody>
    </table></div>`;
  }

  function renderDashboard(data, routes) {
    const id = data.identity ?? {};
    const bal = data.balances ?? {};
    const utxo = data.utxo_summary ?? {};
    const conv = data.convergence_activity ?? {};
    const mint = data.res_mint_activity ?? {};
    const integrity = data.integrity ?? {};
    const txPath = routes.txPath.bind(routes);

    let html = "";

    html += card(
      "Address identity",
      dl([
        ["Address", `<span class="address-mono">${escapeHtml(id.address ?? "")}</span> ${copyBtn(id.address ?? "")}`],
        ["Address type", escapeHtml(id.address_type ?? "—")],
        ["Classification", escapeHtml(id.classification ?? "—")],
        ["First seen block", id.first_seen_block != null ? String(id.first_seen_block) : "—"],
        ["Latest activity block", id.latest_activity_block != null ? String(id.latest_activity_block) : "—"],
        ["Indexed at height", id.indexer_height != null ? String(id.indexer_height) : "—"],
      ]),
    );

    html += card(
      "Asset classification",
      `<p class="muted">Current balance at indexer height ${escapeHtml(String(id.indexer_height ?? data.accounting?.indexer_height ?? "—"))}. This is unspent UTXO, not lifetime received.</p>
      <div class="address-balances">
        <div class="address-balance-box oru"><strong class="address-tag-oru">ORU balance</strong><p>${escapeHtml(bal.oru?.display ?? "0 ORU")}</p></div>
        <div class="address-balance-box res"><strong class="address-tag-res">RES balance</strong><p>${escapeHtml(bal.res?.display ?? "0 RES")}</p></div>
      </div>`,
    );

    html += card(
      "UTXO summary",
      `<div class="address-balances">${renderUtxoSummary("ORU", utxo.oru, "oru")}${renderUtxoSummary("RES", utxo.res, "res")}</div>`,
    );

    html += card("Transaction history", renderHistory(data.transaction_history, routes));

    if (conv.participated) {
      const winnerLinks = (conv.winner_txids ?? [])
        .map((t) => `<a href="${escapeHtml(txPath(t))}" class="address-mono">${escapeHtml(t)}</a>`)
        .join("<br/>");
      html += card(
        "Convergence activity",
        dl([
          ["Convergence tx count", String(conv.convergence_transaction_count ?? 0)],
          ["Mint count", String(conv.winner_count ?? 0)],
          ["Destroy count", String(conv.candidate_count ?? 0)],
          ["Mint transactions", winnerLinks || "—"],
        ]),
      );
    }

    if ((mint.res_mint_count ?? 0) > 0) {
      const mintLinks = (mint.mint_transactions ?? [])
        .map(
          (m) =>
            `<a href="${escapeHtml(txPath(m.mint_txid))}">${escapeHtml(m.mint_txid)}</a> (h=${m.block_height}, ${escapeHtml(m.mint_amount_display)})`,
        )
        .join("<br/>");
      html += card(
        "RES mint activity",
        dl([
          ["RES mint count", String(mint.res_mint_count ?? 0)],
          ["Total RES minted", escapeHtml(mint.total_res_minted_display ?? "0 RES")],
          ["Latest mint block", mint.latest_mint_block != null ? String(mint.latest_mint_block) : "—"],
          ["Mint transactions", mintLinks],
        ]),
      );
    }

    html += card(
      "Address integrity",
      dl([
        ["Chain", escapeHtml(integrity.chain_name ?? "—")],
        ["Genesis (short)", escapeHtml(integrity.genesis_hash_short ?? "—")],
        ["Indexer height", integrity.indexer_height != null ? String(integrity.indexer_height) : "—"],
        ["Indexer lag (blocks)", integrity.indexer_sync_lag_blocks != null ? String(integrity.indexer_sync_lag_blocks) : "—"],
        ["Mismatch active", integrity.mismatch_active ? "yes" : "no"],
      ]),
    );

    return html;
  }

  async function load(page) {
    const root = document.getElementById("address-root");
    const addr = ExplorerRoutes.address() || document.getElementById("addr")?.value?.trim();
    if (!addr || !root) return;
    root.textContent = "Loading…";
    const r = await (window.ExplorerRoutes && ExplorerRoutes.fetchJson
      ? ExplorerRoutes.fetchJson(`/api/address/${encodeURIComponent(addr)}?page=${page || 1}&limit=50`)
      : fetch(`/api/address/${encodeURIComponent(addr)}?page=${page || 1}&limit=50`, { cache: "no-store" }));
    if (!r.ok) {
      root.textContent = "Failed to load address: HTTP " + r.status;
      return;
    }
    const data = await r.json();
    const hist = data.transaction_history ?? {};
    root.innerHTML =
      renderDashboard(data, ExplorerRoutes) +
      `<div class="address-pagination">Page ${hist.page ?? 1} — ${hist.items?.length ?? 0} rows (total ${hist.total ?? 0})
        <button type="button" id="addr-prev" ${(hist.page ?? 1) <= 1 ? "disabled" : ""}>Previous</button>
        <button type="button" id="addr-next" ${(hist.page ?? 1) * (hist.limit ?? 50) >= (hist.total ?? 0) ? "disabled" : ""}>Next</button>
      </div>`;

    if (window.ConvergencePanel?.bindCopyButtons) {
      ConvergencePanel.bindCopyButtons(root);
    } else {
      root.querySelectorAll("[data-copy]").forEach((btn) => {
        btn.addEventListener("click", () => navigator.clipboard.writeText(btn.getAttribute("data-copy") || ""));
      });
    }

    document.getElementById("addr-prev")?.addEventListener("click", () => load((hist.page ?? 1) - 1));
    document.getElementById("addr-next")?.addEventListener("click", () => load((hist.page ?? 1) + 1));
  }

  window.AddressIntelligence = { load, renderDashboard };
})();
