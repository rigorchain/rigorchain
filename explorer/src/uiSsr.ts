/**
 * Server-render Explorer HTML shells so block/tx pages are visible without a
 * second client fetch. Presentation only; data still comes from indexed RPC.
 */

import { readFileSync } from "node:fs";
import { DRAW_FINGERPRINT_LABEL, DRAW_IDENTITY_HEADING } from "./consensusFingerprint";
import { displayBlockConvergenceStatus, displayTxConvergenceRole } from "./convergencePanel";
import { isConvergenceScriptPubKey } from "./script";
import { convergeOutputNote, destinationLabel } from "./txSummary";
import { atomsToResDisplay, formatFixedAtoms } from "./amount";
import { renderResIssuanceChartsHtml } from "./resIssuanceCharts";
import { BLOCKS_PER_YEAR, buildRfc020Curve } from "./resIssuanceSchedule";
import { formatBlockHeaderTimeUtc, formatRelativeTime } from "./relativeTime";

export function escapeHtml(s: unknown): string {
  return String(s ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function shortHash(h: unknown): string {
  const s = String(h ?? "");
  if (!s) return "";
  return s.length > 20 ? `${s.slice(0, 12)}…${s.slice(-6)}` : s;
}

function nestedField(obj: unknown, ...keys: string[]): unknown {
  let cur: unknown = obj;
  for (const k of keys) {
    if (!cur || typeof cur !== "object") return undefined;
    cur = (cur as Record<string, unknown>)[k];
  }
  return cur;
}

function timeAgo(unixSeconds: unknown): string {
  return formatRelativeTime(unixSeconds);
}

function blockHeaderTimeCell(unixSeconds: unknown): string {
  const abs = formatBlockHeaderTimeUtc(unixSeconds);
  if (!abs) return "—";
  const rel = timeAgo(unixSeconds);
  return `${escapeHtml(abs)}<div class="muted">${escapeHtml(rel)}</div>`;
}

function timeListCell(unixSeconds: unknown): string {
  const abs = formatBlockHeaderTimeUtc(unixSeconds);
  if (!abs) return "—";
  return `<span title="${escapeHtml(abs)}">${escapeHtml(timeAgo(unixSeconds))}</span>`;
}

export function injectSsrRoot(templateHtml: string, innerHtml: string): string {
  const rendered = `<div id="root" data-ssr="1">${innerHtml}</div>`;
  if (templateHtml.includes('<div id="root">Loading...</div>')) {
    return templateHtml.replace('<div id="root">Loading...</div>', rendered);
  }
  if (templateHtml.includes('<div id="root">Loading…</div>')) {
    return templateHtml.replace('<div id="root">Loading…</div>', rendered);
  }
  if (templateHtml.includes('id="status-root"')) {
    return templateHtml.replace(
      /<div id="status-root"[^>]*>Loading…<\/div>/,
      `<div id="status-root" class="status-dashboard" data-ssr="1">${innerHtml}</div>`,
    );
  }
  return templateHtml;
}

export function readPublicTemplate(publicDir: string, file: string): string {
  return readFileSync(`${publicDir}/${file}`, "utf8");
}

function renderBlockConvergence(panel: Record<string, unknown> | null | undefined): string {
  if (!panel) return "";
  const status = escapeHtml(
    displayBlockConvergenceStatus(
      (panel.status as "No Convergence Activity" | "Convergence Candidate Block" | "Convergence Winner Block") ??
        "No Convergence Activity",
    ),
  );
  const fSize = escapeHtml(panel.candidate_set_size ?? 0);
  const ids = Array.isArray(panel.candidate_txids) ? panel.candidate_txids : [];
  const list =
    ids.length > 0
      ? ids
          .map((id) => `<li><a href="/tx/${encodeURIComponent(String(id))}">${escapeHtml(id)}</a></li>`)
          .join("")
      : '<li class="muted">No OP_CONVERGE destroys indexed for this block.</li>';
  return `<section class="convergence-panel" id="convergence-panel">
    <h2>Convergence</h2>
    <dl>
      <dt>Convergence Status</dt><dd><span class="convergence-status">${status}</span></dd>
      <dt>Convergence destroys</dt><dd>${fSize}</dd>
      <dt>convergence_txids</dt><dd><ul class="convergence-candidate-list">${list}</ul></dd>
    </dl>
  </section>`;
}

export function renderBlockRoot(data: Record<string, unknown>, hash: string): string {
  const b = (data.block as Record<string, unknown>) || {};
  const s = (data.summary as Record<string, unknown>) || {};
  const panel = data.convergence_panel as Record<string, unknown> | undefined;
  const txs = (data.txs as Array<Record<string, unknown>>) || [];
  const height = s.height ?? b.height ?? "";
  const blockHash = String(s.hash ?? b.block_hash ?? hash);
  const prev = String(s.previous_block_hash ?? b.previous_block_hash ?? "");
  const fp = String(s.consensus_fingerprint_display ?? b.consensus_fingerprint_display ?? "Not available");
  const fpStatus = String(s.consensus_fingerprint_status ?? b.consensus_fingerprint_status ?? "Not available");
  const t = s.time ?? b.time;
  const rows = txs
    .map((tx, idx) => {
      return `<tr>
        <td>${idx}</td>
        <td>${escapeHtml(tx.tx_kind_label ?? (tx.is_coinbase ? "Coinbase" : ""))}</td>
        <td>${escapeHtml(tx.amount_display ?? "—")}</td>
        <td>${escapeHtml(tx.asset_kind ?? "—")}</td>
        <td><a href="/tx/${encodeURIComponent(String(tx.txid ?? ""))}">${escapeHtml(shortHash(tx.txid))}</a></td>
      </tr>`;
    })
    .join("");
  return `<section class="verify-independently-panel">
      <h2>Block #${escapeHtml(height)}</h2>
      <p class="mono">Hash: ${escapeHtml(blockHash)}</p>
      <p>Previous Block: ${prev ? `<a class="mono" href="/block/${encodeURIComponent(prev)}">${escapeHtml(prev)}</a>` : "N/A"}</p>
      <section class="consensus-identity-panel">
        <h3>${DRAW_IDENTITY_HEADING}</h3>
        <p>${DRAW_FINGERPRINT_LABEL}:</p>
        <p class="mono consensus-fingerprint-value">${escapeHtml(fp)}</p>
        <p>Status: ${escapeHtml(fpStatus)}</p>
      </section>
      <p class="muted">Rendered by Explorer from indexed rigord data (no second browser fetch required).</p>
    </section>
    ${renderBlockConvergence(panel)}
    <h2>Summary</h2>
    <table><tbody>
      <tr><th>Height</th><td>${escapeHtml(height)}</td></tr>
      <tr><th>Hash</th><td class="mono">${escapeHtml(blockHash)}</td></tr>
      <tr><th>nBits</th><td class="mono">${escapeHtml(s.bits ?? b.bits ?? "N/A")}</td></tr>
      <tr><th>OpuHash</th><td class="mono">${escapeHtml(s.opus_hash ?? b.opus_hash ?? b.pow_hash ?? "N/A")}</td></tr>
      <tr><th>Block header time (UTC)</th><td>${blockHeaderTimeCell(t)}</td></tr>
      <tr><th>Coinbase</th><td class="mono">${s.coinbase_txid ? `<a href="/tx/${encodeURIComponent(String(s.coinbase_txid))}">${escapeHtml(s.coinbase_txid)}</a>` : ""}</td></tr>
      <tr><th>ORU subsidy</th><td>${escapeHtml(s.oru_subsidy_display ?? "N/A")}</td></tr>
      <tr><th>Fees destroyed (this block)</th><td>${escapeHtml(
        nestedField(data.rfc018_fee_burn, "block_burned_oru_display") ?? "—",
      )}</td></tr>
      <tr><th>Fees destroyed (cumulative)</th><td>${escapeHtml(
        nestedField(data.rfc018_fee_burn, "cumulative_burned_oru_display") ?? "—",
      )}</td></tr>
      <tr><th>RES minted</th><td>${escapeHtml(s.res_minted_display ?? "0")}</td></tr>
      ${
        s.res_destroy_cost_display
          ? `<tr><th>ORU destroyed to mint 1 RES</th><td>${escapeHtml(s.res_destroy_cost_display)}</td></tr>`
          : ""
      }
      <tr><th>bind_height</th><td>${escapeHtml(s.bind_height_display ?? "Not available")}</td></tr>
    </tbody></table>
    <h2>Header</h2>
    <pre class="mono">${escapeHtml(JSON.stringify(publicBlockHeader(b), null, 2))}</pre>
    <h2>Transactions</h2>
    <table>
      <thead><tr><th>#</th><th>kind</th><th>amount</th><th>asset</th><th>txid</th></tr></thead>
      <tbody>${rows}</tbody>
    </table>`;
}

function renderTxConvergence(panel: Record<string, unknown> | null | undefined): string {
  if (!panel) return "";
  let heightHtml = '<span class="muted">—</span>';
  if (panel.block_height != null) {
    heightHtml = escapeHtml(panel.block_height);
    if (panel.block_hash) {
      heightHtml += ` (<a href="/block/${encodeURIComponent(String(panel.block_hash))}">block</a>)`;
    }
    // RFC-020: no sliding candidate window. Inclusion height is the confirming height.
  }
  return `<section class="convergence-panel" id="convergence-panel">
    <h2>Convergence</h2>
    <dl>
      <dt>Convergence Role</dt><dd><span class="convergence-status">${escapeHtml(
        displayTxConvergenceRole(
          (panel.role as "Candidate" | "Winner" | "Not Participating") ?? "Not Participating",
        ),
      )}</span></dd>
      <dt>Confirming height</dt><dd>${heightHtml}</dd>
    </dl>
  </section>`;
}

export function renderTxRoot(data: Record<string, unknown>, txid: string): string {
  const sum = (data.summary as Record<string, unknown>) || {};
  const fee = (sum.fee as Record<string, unknown>) || {};
  const panel = data.convergence_panel as Record<string, unknown> | undefined;
  const vout = (data.vout as Array<Record<string, unknown>>) || [];
  const destLabel = destinationLabel(typeof sum.amount_role === "string" ? sum.amount_role : null);
  const amountLabel = sum.is_coinbase ? "Subsidy" : "Amount";
  const amount = sum.is_coinbase ? (sum.subsidy_display ?? sum.amount_display) : sum.amount_display;
  const extra =
    !sum.is_coinbase && sum.amount_total_display && sum.amount_total_display !== sum.amount_display
      ? ` <span class="muted">(outputs total ${escapeHtml(sum.amount_total_display)})</span>`
      : "";
  const txKind = typeof sum.tx_kind === "string" ? sum.tx_kind : "";
  const outputNote =
    typeof sum.converge_output_note === "string" && sum.converge_output_note
      ? String(sum.converge_output_note)
      : convergeOutputNote(txKind === "oru_destroy" || txKind === "res_mint" ? txKind : "convergence_lock");
  const notice =
    typeof sum.kind_notice === "string" && sum.kind_notice
      ? `<p class="tx-kind-notice${txKind === "res_mint" ? " mint" : ""}">${escapeHtml(sum.kind_notice)}</p>`
      : "";
  const typeRow =
    typeof sum.tx_kind_label === "string" && sum.tx_kind_label
      ? `<tr><th>Type</th><td>${escapeHtml(sum.tx_kind_label)}</td></tr>`
      : "";
  const voutRows = vout
    .map((o) => {
      const kind = String(o.asset_kind ?? "");
      const atoms = String(o.value_atoms ?? "0");
      let valueStr = atoms;
      try {
        if (kind === "ORU") {
          valueStr = formatFixedAtoms(BigInt(atoms), 8);
        } else if (kind === "RES") {
          valueStr = formatFixedAtoms(BigInt(atoms), 8);
        }
      } catch {
        /* keep raw */
      }
      const isConverge =
        Boolean(o.is_convergence) ||
        (typeof o.script_pubkey === "string" && isConvergenceScriptPubKey(o.script_pubkey));
      const convergeNote = isConverge ? ` <span class="muted">${escapeHtml(outputNote)}</span>` : "";
      const addr = o.address
        ? `<a href="/address/${encodeURIComponent(String(o.address))}">${escapeHtml(o.address)}</a>${convergeNote}`
        : isConverge
          ? `<span class="muted">${escapeHtml(outputNote)}</span>`
          : "";
      return `<tr><td>${escapeHtml(o.vout_index)}</td><td>${escapeHtml(kind)}</td><td>${escapeHtml(valueStr)}</td><td>${addr}</td></tr>`;
    })
    .join("");
  return `<section class="verify-independently-panel">
      <h2>Transaction</h2>
      <p class="mono">TXID: ${escapeHtml(txid)}</p>
      <p class="muted">${DRAW_FINGERPRINT_LABEL}: see the including block (observational short ID, not a consensus check).</p>
    </section>
    ${notice}
    ${renderTxConvergence(panel)}
    <h2>Summary</h2>
    <table><tbody>
      ${typeRow}
      <tr><th>Sender</th><td class="mono">${escapeHtml(sum.sender ?? "N/A")}</td></tr>
      <tr><th>${escapeHtml(destLabel)}</th><td class="mono">${sum.receiver ? `<a href="/address/${encodeURIComponent(String(sum.receiver))}">${escapeHtml(sum.receiver)}</a>` : "N/A"}</td></tr>
      <tr><th>${escapeHtml(amountLabel)}</th><td>${escapeHtml(amount ?? "N/A")}${extra}</td></tr>
      <tr><th>Fee</th><td>${escapeHtml(fee.fee_display ?? "N/A")}</td></tr>
    </tbody></table>
    <h2>Outputs</h2>
    <table>
      <thead><tr><th>vout</th><th>asset</th><th>amount</th><th>address</th></tr></thead>
      <tbody>${voutRows}</tbody>
    </table>`;
}

export function renderBlocksRoot(
  blocks: Array<Record<string, unknown>>,
  page?: { page: number; pages: number; total: number; has_prev: boolean; has_next: boolean },
): string {
  const rows = blocks
    .map(
      (b) => `<tr>
        <td>${escapeHtml(b.height)}</td>
        <td><a href="/block/${encodeURIComponent(String(b.block_hash ?? ""))}">${escapeHtml(shortHash(b.block_hash))}</a></td>
        <td>${timeListCell(b.time)}</td>
        <td>${escapeHtml(b.tx_count)}</td>
        <td>${escapeHtml(b.oru_minted_display ?? b.oru_minted ?? "—")}</td>
        <td>${escapeHtml(b.res_minted_display ?? "—")}</td>
        <td>${escapeHtml(b.converge_burn_display ?? b.candidate_set_size ?? 0)}</td>
      </tr>`,
    )
    .join("");
  const pager = page
    ? `<p class="muted blocks-pager">Page ${page.page} / ${page.pages} · ${page.total} blocks${
        page.has_prev ? ` · <a href="/blocks?page=${page.page - 1}">Newer</a>` : ""
      }${page.has_next ? ` · <a href="/blocks?page=${page.page + 1}">Older</a>` : ""}</p>`
    : "";
  return `${pager}<table>
    <thead><tr><th>height</th><th>hash</th><th>time</th><th>tx_count</th><th>oru_minted</th><th>res_minted</th><th>converge_destroy</th></tr></thead>
    <tbody>${rows}</tbody>
  </table>${pager}`;
}

function statusFmt(v: unknown): string {
  if (v === null || v === undefined || v === "") return "—";
  return escapeHtml(v);
}

function statusMonoHash(h: unknown): string {
  const s = String(h ?? "");
  if (!s) return "—";
  return `<span class="status-mono" title="${escapeHtml(s)}">${escapeHtml(shortHash(s))}</span>`;
}

function statusDl(label: string, valueHtml: string): string {
  return `<dt>${escapeHtml(label)}</dt><dd>${valueHtml}</dd>`;
}

function statusSection(title: string, rows: string[]): string {
  return `<section class="status-card"><h2>${escapeHtml(title)}</h2><dl>${rows.join("")}</dl></section>`;
}

function statusNetworkLabel(id: Record<string, unknown>): string {
  const n = String(id.network ?? id.network_type ?? "").trim();
  return n ? n.toUpperCase() : "—";
}

const BLOCK_HEADER_PUBLIC = new Set([
  "height",
  "block_hash",
  "previous_block_hash",
  "version",
  "time",
  "bits",
  "nonce",
  "merkleroot",
  "opus_hash",
  "pow_hash",
  "tx_count",
  "consensus_fingerprint",
  "consensus_fingerprint_display",
  "consensus_fingerprint_status",
  "rigor_res_minted",
]);

function publicBlockHeader(block: Record<string, unknown>): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const [k, v] of Object.entries(block)) {
    if (BLOCK_HEADER_PUBLIC.has(k)) out[k] = v;
  }
  return out;
}

/** Same homepage cards as public/status.js renderDashboard. Each fact appears once. */
export function renderStatusRoot(data: Record<string, unknown>): string {
  const id = (data.chain_identity as Record<string, unknown>) || {};
  const bh = (data.block_health as Record<string, unknown>) || {};
  const eco = (data.economic_snapshot as Record<string, unknown>) || {};
  const mine = (data.mining_activity as Record<string, unknown>) || {};
  const sys = (data.system_integrity as Record<string, unknown>) || {};
  const comp = (sys.explorer_compliance as Record<string, unknown>) || {};
  const issued =
    nestedField(eco.supply_chart, "total_issued_oru", "display") ?? eco.oru_supply_cumulative_display;
  const net = statusNetworkLabel(id);
  const avg =
    bh.avg_block_time_seconds_last_100 != null
      ? `${Number(bh.avg_block_time_seconds_last_100).toFixed(1)} s`
      : "—";

  const indexerRows = [
    statusDl("Height (indexer)", statusFmt(bh.current_height_indexer)),
    statusDl("Indexer lag (blocks)", statusFmt(sys.indexer_sync_lag_blocks)),
    statusDl("Indexer lag (seconds)", statusFmt(sys.indexer_sync_lag_seconds)),
    statusDl("Mismatch active", comp.mismatch_active ? "yes" : "no"),
    statusDl(
      "Reorg signal",
      `${statusFmt(bh.reorg_signal_blocks_mismatch_last_1000)} mismatch blocks / 1000`,
    ),
    statusDl("IBD", statusFmt(bh.initialblockdownload)),
  ];
  if (sys.rpc_error) {
    indexerRows.push(statusDl("RPC", statusFmt(sys.rpc_error)));
  }

  const sections = [
    statusSection("Chain identity", [
      statusDl("Chain", statusFmt(id.chain_name)),
      statusDl("Network", statusFmt(net)),
      statusDl("Genesis", statusMonoHash(id.genesis_hash)),
      statusDl("Node", statusFmt(id.product ?? id.node_subversion ?? id.node_version)),
      statusDl("Subversion", statusFmt(id.node_subversion)),
      statusDl("Best block", statusMonoHash(bh.best_block_hash)),
    ]),
    statusSection("Block production", [
      statusDl("Height (node)", statusFmt(bh.current_height_node)),
      statusDl("Avg block time (100)", avg),
      statusDl("Difficulty", statusFmt(bh.difficulty)),
      statusDl("nBits", statusFmt(bh.bits)),
      statusDl("OpuHash", statusMonoHash(bh.opus_hash ?? bh.pow_hash)),
      statusDl("Peers", statusFmt(mine.connections)),
      statusDl("Mempool tx count", statusFmt(mine.mempool_tx_count)),
    ]),
    statusSection("Indexer integrity", indexerRows),
    statusSection("Issuance", [
      statusDl("Issued ORU", statusFmt(issued)),
      statusDl("RES minted", statusFmt(eco.network_res_minted_display)),
      statusDl("Convergence destroyed ORU", statusFmt(eco.cumulative_convergence_display ?? eco.oru_burned_lifetime_display)),
      statusDl("ORU destroyed to mint 1 RES", statusFmt(eco.res_destroy_cost_display)),
      statusDl("Confirming height", statusFmt(eco.confirm_height ?? eco.tip_height)),
      statusDl("ORU subsidy", statusFmt(eco.oru_subsidy_tip_display)),
      statusDl("Last mint height", statusFmt(eco.last_res_height)),
    ]),
  ];

  return `<p class="network-mark">NETWORK <strong>${escapeHtml(net)}</strong></p><div class="status-grid status-grid--pair">${sections.join("")}</div>`;
}

export function injectSsrResPage(templateHtml: string, curvesHtml: string, rootHtml: string): string {
  return templateHtml
    .replace(
      /<div id="curves"[^>]*>[\s\S]*?<\/div>/,
      `<div id="curves" class="status-dashboard" data-ssr="1">${curvesHtml}</div>`,
    )
    .replace(
      /<div id="root"[^>]*>Loading(?: reserve activity)?…<\/div>/,
      `<div id="root" class="status-dashboard" data-ssr="1">${rootHtml}</div>`,
    );
}

function resTxLink(txid: unknown): string {
  const s = String(txid ?? "");
  if (!s || /^0+$/.test(s)) return "—";
  const label = s.length < 20 ? s : `${s.slice(0, 12)}…${s.slice(-6)}`;
  return `<a class="mono" href="/tx/${encodeURIComponent(s)}" title="${escapeHtml(s)}">${escapeHtml(label)}</a>`;
}

export function renderResDetailsHtml(
  eco: Record<string, unknown>,
  mints: Array<Record<string, unknown>>,
): string {
  const schedule = `<section class="status-card"><h2>RES schedule</h2><dl>
    <dt>ORU destroyed to mint 1 RES</dt><dd>${escapeHtml(eco.res_destroy_cost_display ?? "—")}</dd>
    <dt>Confirming height</dt><dd>${escapeHtml(eco.confirm_height ?? eco.tip_height ?? eco.height ?? "—")}</dd>
    <dt>RES mint ceiling</dt><dd>${escapeHtml(eco.res_derived_max_supply_display ?? "—")}</dd>
    <dt>Network RES minted</dt><dd>${escapeHtml(eco.network_res_minted_display ?? "—")}</dd>
  </dl></section>`;
  const activity = `<section class="status-card"><h2>RES activity</h2><dl>
    <dt>RES minted (last 100)</dt><dd>${escapeHtml(eco.res_mint_rate_last_100_blocks_display ?? "—")}</dd>
    <dt>Mint count</dt><dd>${escapeHtml(eco.res_mint_count ?? "—")}</dd>
    <dt>Last mint height</dt><dd>${escapeHtml(eco.last_res_height ?? "—")}</dd>
    <dt>Last mint tx</dt><dd>${resTxLink(eco.last_res_mint_txid)}</dd>
  </dl></section>`;
  const rows = mints
    .map((m) => {
      const amount =
        m.mint_amount != null
          ? atomsToResDisplay(String(m.mint_amount))
          : "—";
      return `<tr><td>${escapeHtml(m.block_height)}</td><td><a class="mono" href="/tx/${encodeURIComponent(String(m.mint_txid ?? ""))}">${escapeHtml(m.mint_txid)}</a></td><td>${escapeHtml(amount)}</td></tr>`;
    })
    .join("");
  return (
    `<div class="status-grid status-grid--pair">${schedule}${activity}</div>` +
    `<div class="res-history"><h2>RES mint history</h2>` +
    `<table><thead><tr><th>height</th><th>mint txid</th><th>amount</th></tr></thead><tbody>` +
    (rows || '<tr><td colspan="3" class="muted">No RES mints on this chain yet.</td></tr>') +
    `</tbody></table></div>`
  );
}

export function renderResPageSsr(args: {
  confirmHeight: number | null;
  eco: Record<string, unknown>;
  mints: Array<Record<string, unknown>>;
}): { curvesHtml: string; rootHtml: string } {
  const currentYear =
    args.confirmHeight != null && Number.isFinite(args.confirmHeight)
      ? args.confirmHeight / BLOCKS_PER_YEAR
      : null;
  return {
    curvesHtml: renderResIssuanceChartsHtml(buildRfc020Curve(), currentYear),
    rootHtml: renderResDetailsHtml(args.eco, args.mints),
  };
}
