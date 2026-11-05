import { Pool } from "pg";
import type { Config } from "./config";
import { buildBlockConvergencePanel, buildTxConvergencePanel } from "./convergencePanel";
import { buildConvergeProofPanel } from "./convergeProofPanel";
import { getLineageForBlock, getLineageForTxid } from "./lineageDb";
import { buildTxWitnessPanel } from "./witnessPanel";
import { computeTxFee, formatOruAtoms } from "./fee";
import { classifyTxKind, convergeOutputNote, pickPrimaryOruOutput, presentBlockTx, txKindLabel, txKindNotice } from "./txSummary";
import {
  CONVERGENCE_OUTPUT_STATE,
  OP_CONVERGE_SCRIPT_NAME,
  isConvergenceScriptPubKey,
} from "./script";
import { displayFingerprint, fingerprintStatus, shortFingerprint } from "./consensusFingerprint";
import { atomsToOruDisplay, atomsToResDisplay } from "./addressIntelligence";
import { displayOrNA, displayOrNotAvailable } from "./rpcFields";
import { getRfc018FeeBurnAtHeight } from "./rfc018FeeBurnStore";
import { rfc018FeeBurnView } from "./rfc018FeeBurn";
import { formatRateOruPerRes, resDestroyCostForHeight } from "./resIssuanceSchedule";
import { opConvergeScriptSql } from "./circulating";
import { CONVERGENCE_IS_MINT_SQL } from "./rfc020MintSql";

export type ChainStateRow = {
  indexer_name: string;
  network: string;
  last_height: bigint;
  last_block_hash: string | null;
  critical_root_mismatch: boolean;
  issuance_generation: string | null;
  updated_at: Date;
};

export function createPool(config: Config): Pool {
  return new Pool({
    connectionString: config.pgUrl,
    // Determinism note:
    // - keep transaction serialization under our control by using explicit SQL.
    max: 5,
  });
}

const MIGRATION_FILES = [
  "001_init.sql",
  "002_alphaplus_compat.sql",
  "003_c_root.sql",
  "004_consensus_fingerprint.sql",
  "005_rfc018_fee_burn_stats.sql",
  "007_issuance_generation.sql",
];

export async function ensureSchema(pool: Pool, config: Config): Promise<void> {
  const fs = await import("node:fs/promises");
  const path = await import("node:path");

  await pool.query(`
    CREATE TABLE IF NOT EXISTS schema_migrations (
      id TEXT PRIMARY KEY,
      applied_at TIMESTAMPTZ NOT NULL DEFAULT now()
    )
  `);

  const existing = await pool.query(`SELECT to_regclass('public.chain_state') as ok`);
  if (existing.rows[0]?.ok) {
    await pool.query(
      `INSERT INTO schema_migrations(id) VALUES ('001_init.sql') ON CONFLICT (id) DO NOTHING`,
    );
  }

  for (const file of MIGRATION_FILES) {
    const applied = await pool.query(`SELECT 1 FROM schema_migrations WHERE id = $1`, [file]);
    if (applied.rowCount) continue;

    const sql = await fs.readFile(path.join(config.schemaMigrationsPath, file), "utf8");
    await pool.query(sql);
    await pool.query(`INSERT INTO schema_migrations(id) VALUES ($1)`, [file]);
  }
}

export async function getOrCreateChainState(pool: Pool, config: Config): Promise<ChainStateRow> {
  const network = config.network;
  const res = await pool.query<ChainStateRow>(
    `
      SELECT indexer_name, network, last_height, last_block_hash, critical_root_mismatch, issuance_generation, updated_at
      FROM chain_state WHERE indexer_name = $1
    `,
    [config.indexerName],
  );
  if (res.rowCount && res.rows[0]) return res.rows[0];

  await pool.query(
    `
      INSERT INTO chain_state(indexer_name, network, last_height, last_block_hash, critical_root_mismatch, issuance_generation, updated_at)
      VALUES($1, $2, -1, NULL, FALSE, NULL, now())
    `,
    [config.indexerName, network],
  );

  const res2 = await pool.query<ChainStateRow>(
    `
      SELECT indexer_name, network, last_height, last_block_hash, critical_root_mismatch, issuance_generation, updated_at
      FROM chain_state WHERE indexer_name = $1
    `,
    [config.indexerName],
  );
  return res2.rows[0];
}

export async function stampIssuanceGeneration(
  pool: Pool,
  config: Config,
  generation: string,
): Promise<void> {
  await pool.query(
    `
      UPDATE chain_state
      SET issuance_generation = $2, updated_at = now()
      WHERE indexer_name = $1
    `,
    [config.indexerName, generation],
  );
}

export async function updateChainState(pool: Pool, config: Config, patch: Partial<Pick<ChainStateRow, "last_height" | "last_block_hash" | "critical_root_mismatch">>): Promise<void> {
  const cur = await getOrCreateChainState(pool, config);
  const nextLastHeight = patch.last_height ?? cur.last_height;
  const nextLastBlockHash = patch.last_block_hash ?? cur.last_block_hash;
  const nextCritical = patch.critical_root_mismatch ?? cur.critical_root_mismatch;

  await pool.query(
    `
      UPDATE chain_state
      SET last_height = $2,
          last_block_hash = $3,
          critical_root_mismatch = $4,
          updated_at = now()
      WHERE indexer_name = $1
    `,
    [config.indexerName, nextLastHeight, nextLastBlockHash, nextCritical],
  );
}

export async function truncateFromHeight(pool: Pool, height: bigint): Promise<void> {
  // Cascade delete via FK graph.
  await pool.query(`DELETE FROM blocks WHERE height >= $1`, [height]);
}

// --------------------
// Query helpers for API
// --------------------

export async function listBlocks(pool: Pool, limit: number, offset = 0): Promise<any[]> {
  const res = await pool.query(
    `
      SELECT b.height, b.block_hash, b.time, b.tx_count,
             b.rigor_winner,
             b.consensus_fingerprint,
             b.draw_candidate_root,
             b.committed_root,
             b.computed_root,
             COALESCE((
               SELECT COUNT(*)::int
               FROM convergence_events ce
               WHERE ce.block_hash = b.block_hash
             ), 0) AS candidate_set_size,
             COALESCE((
               SELECT SUM(o.value_atoms)::text
               FROM transactions t
               JOIN outputs o ON o.txid = t.txid AND o.asset_kind = 'ORU'
               WHERE t.block_hash = b.block_hash
                 AND t.block_tx_index = 0
             ), '0') AS oru_minted_atoms,
             COALESCE((
               SELECT SUM(rm.mint_amount)::text
               FROM res_mints rm
               WHERE rm.block_hash = b.block_hash
             ), '0') AS res_minted_atoms,
             COALESCE((
               SELECT SUM(o.value_atoms)::text
               FROM transactions t
               JOIN outputs o ON o.txid = t.txid
               WHERE t.block_hash = b.block_hash
                 AND LOWER(REPLACE(TRIM(o.script_pubkey), ' ', '')) ~ '^14[0-9a-f]{40}bd$'
             ), '0') AS converge_burn_atoms,
             s.burned_oru::text AS rfc018_block_burned_oru,
             CASE
               WHEN s.height IS NULL THEN NULL
               WHEN EXISTS (
                 SELECT 1
                 FROM blocks earlier
                 WHERE earlier.height <= b.height
                   AND NOT EXISTS (
                     SELECT 1
                     FROM rfc018_fee_burn_stats x
                     WHERE x.height = earlier.height
                       AND x.block_hash = earlier.block_hash
                   )
               ) THEN NULL
               ELSE (
                 SELECT COALESCE(SUM(x.burned_oru), 0)::text
                 FROM rfc018_fee_burn_stats x
                 JOIN blocks earlier ON earlier.height = x.height AND earlier.block_hash = x.block_hash
                 WHERE x.height <= b.height
               )
             END AS rfc018_cumulative_burned_oru
      FROM blocks b
      LEFT JOIN rfc018_fee_burn_stats s ON s.height = b.height AND s.block_hash = b.block_hash
      ORDER BY b.height DESC
      LIMIT $1 OFFSET $2
    `,
    [limit, Math.max(0, offset)],
  );
  return res.rows.map((row) => {
    const atoms = BigInt(row.oru_minted_atoms ?? "0");
    const resMintAtoms = BigInt(row.res_minted_atoms ?? "0");
    const convergeBurnAtoms = BigInt(row.converge_burn_atoms ?? "0");
    const blockBurn = row.rfc018_block_burned_oru == null ? null : BigInt(row.rfc018_block_burned_oru);
    const cumBurn = row.rfc018_cumulative_burned_oru == null ? null : BigInt(row.rfc018_cumulative_burned_oru);
    const indexed = blockBurn != null && cumBurn != null;
    return {
      ...row,
      oru_minted: formatOruAtoms(atoms),
      oru_minted_display: `${formatOruAtoms(atoms)} ORU`,
      res_minted_display: resMintAtoms > 0n ? atomsToResDisplay(resMintAtoms) : "—",
      converge_burn_display: convergeBurnAtoms > 0n ? atomsToOruDisplay(convergeBurnAtoms) : "—",
      consensus_fingerprint: row.consensus_fingerprint ?? null,
      consensus_fingerprint_short: shortFingerprint(row.consensus_fingerprint),
      consensus_fingerprint_display: displayFingerprint(row.consensus_fingerprint),
      consensus_fingerprint_status: fingerprintStatus(row.consensus_fingerprint),
      rfc018_fee_burn: indexed
        ? {
            ...rfc018FeeBurnView(blockBurn, cumBurn, atomsToOruDisplay),
            indexed: true,
          }
        : {
            block_burned_oru: blockBurn == null ? null : blockBurn.toString(),
            cumulative_burned_oru: null,
            block_burned_oru_display: blockBurn == null ? null : atomsToOruDisplay(blockBurn),
            cumulative_burned_oru_display: null,
            equivalence_claimed: false as const,
            indexed: false,
          },
    };
  });
}

export async function countBlocks(pool: Pool): Promise<number> {
  const res = await pool.query<{ n: number }>(`SELECT COUNT(*)::int AS n FROM blocks`);
  return Number(res.rows[0]?.n ?? 0);
}

export async function getBlockHashByHeight(pool: Pool, height: string | number | bigint): Promise<string | null> {
  const res = await pool.query(
    `SELECT block_hash FROM blocks WHERE height = $1::bigint LIMIT 1`,
    [String(height)],
  );
  return res.rows[0]?.block_hash ?? null;
}

/** Canonical txid for a witness txid. Null when wtxid is missing or equals txid. */
export async function getTxidByWtxid(pool: Pool, wtxid: string): Promise<string | null> {
  const hex = wtxid.trim().toLowerCase();
  const res = await pool.query<{ txid: string }>(
    `SELECT txid FROM transactions WHERE wtxid = $1 AND txid <> $1 LIMIT 1`,
    [hex],
  );
  return res.rows[0]?.txid ?? null;
}

export async function getBlockByHash(pool: Pool, hash: string): Promise<any | null> {
  const b = await pool.query(
    `
      SELECT *
      FROM blocks
      WHERE block_hash = $1
    `,
    [hash],
  );
  if (!b.rowCount) return null;

  const txs = await pool.query(
    `
      SELECT txid, wtxid, block_tx_index, is_coinbase
      FROM transactions
      WHERE block_hash = $1
      ORDER BY block_tx_index ASC
    `,
    [hash],
  );

  const txOuts = await pool.query(
    `
      SELECT txid, vout_index, value_atoms, asset_kind, address, witness_version, script_pubkey
      FROM outputs
      WHERE txid = ANY($1::text[])
      ORDER BY txid ASC, vout_index ASC
    `,
    [txs.rows.map((t) => t.txid)],
  );
  const outsByTx = new Map<string, typeof txOuts.rows>();
  for (const o of txOuts.rows) {
    const list = outsByTx.get(o.txid) ?? [];
    list.push(o);
    outsByTx.set(o.txid, list);
  }

  const block = { ...b.rows[0] };
  delete block.c_root;
  const resMinted = Number(block.rigor_res_minted ?? 0);
  const mintRows = await pool.query<{ mint_txid: string; mint_amount: string }>(
    `SELECT mint_txid, mint_amount::text AS mint_amount FROM res_mints WHERE block_hash = $1`,
    [hash],
  );
  const mintTxids = new Set(mintRows.rows.map((r) => r.mint_txid));
  const resInputRows = txs.rows.length
    ? await pool.query<{ txid: string }>(
        `
          SELECT DISTINCT i.txid
          FROM inputs i
          JOIN outputs prev ON prev.txid = i.prev_txid AND prev.vout_index = i.prev_vout
          JOIN transactions pt ON pt.txid = prev.txid
          JOIN blocks pb ON pb.block_hash = pt.block_hash
          WHERE i.txid = ANY($1::text[])
            AND prev.asset_kind = 'RES'
            AND prev.value_atoms > 0
        `,
        [txs.rows.map((t) => t.txid)],
      )
    : { rows: [] as Array<{ txid: string }> };
  const resInputTxids = new Set(resInputRows.rows.map((row) => row.txid));

  const enrichedTxs = txs.rows.map((t) => {
    const outs = outsByTx.get(t.txid) ?? [];
    return {
      txid: t.txid,
      wtxid: t.wtxid,
      block_tx_index: t.block_tx_index,
      is_coinbase: Boolean(t.is_coinbase),
      ...presentBlockTx({
        isCoinbase: Boolean(t.is_coinbase),
        isResMint: mintTxids.has(t.txid),
        outputs: outs,
        hasPositiveResInput: resInputTxids.has(t.txid),
      }),
    };
  });

  const candidates = await pool.query(
    `
      SELECT convergence_txid, draw_eligible
      FROM convergence_events
      WHERE block_hash = $1
      ORDER BY convergence_txid ASC
    `,
    [hash],
  );

  const convergence_panel = buildBlockConvergencePanel(
    {
      ...block,
      mint_txid: mintRows.rows[0]?.mint_txid ?? null,
      rigor_res_minted: resMinted,
    },
    candidates.rows,
  );

  // Legacy field: winner row for audit consumers.
  const winnerRow = candidates.rows.find((c) => c.convergence_txid === (block.rigor_winner || "").trim()) ?? null;

  const lineage_graph = await getLineageForBlock(pool, hash);

  const coinbaseTx = txs.rows[0] ?? null;
  let oruSubsidyAtoms = 0n;
  if (coinbaseTx?.txid) {
    const coinOuts = await pool.query(
      `SELECT value_atoms FROM outputs WHERE txid = $1 AND asset_kind = 'ORU'`,
      [coinbaseTx.txid],
    );
    for (const o of coinOuts.rows) oruSubsidyAtoms += BigInt(o.value_atoms);
  }

  let previous_block_hash: string | null = null;
  try {
    const h = BigInt(block.height);
    if (h > 0n) {
      const prev = await pool.query(`SELECT block_hash FROM blocks WHERE height = $1 LIMIT 1`, [h - 1n]);
      previous_block_hash = prev.rows[0]?.block_hash ?? null;
    }
  } catch {
    previous_block_hash = null;
  }

  let bodyResAtoms = 0n;
  for (const t of txs.rows) {
    if (t.is_coinbase) continue;
    for (const o of outsByTx.get(t.txid) ?? []) {
      if (o.asset_kind === "RES") bodyResAtoms += BigInt(o.value_atoms);
    }
  }
  const indexedMintAtoms = mintRows.rows.reduce((acc, r) => acc + BigInt(r.mint_amount ?? 0), 0n);
  const mintAtoms = indexedMintAtoms > 0n ? indexedMintAtoms : bodyResAtoms;
  const res_minted_display = mintAtoms > 0n ? atomsToResDisplay(mintAtoms) : resMinted ? String(resMinted) : "0";
  const heightNum = Number(block.height);
  const res_destroy_cost_display = Number.isFinite(heightNum)
    ? formatRateOruPerRes(resDestroyCostForHeight(heightNum))
    : null;

  const fp = block.consensus_fingerprint ?? null;
  let feeBurnRow: Awaited<ReturnType<typeof getRfc018FeeBurnAtHeight>> = null;
  try {
    feeBurnRow = await getRfc018FeeBurnAtHeight(pool, block.height);
  } catch {
    feeBurnRow = null;
  }
  const feeCumulative = feeBurnRow?.cumulative_burned_oru ?? null;
  const rfc018_fee_burn =
    feeBurnRow != null && feeCumulative != null
      ? {
          ...rfc018FeeBurnView(feeBurnRow.burned_oru, feeCumulative, atomsToOruDisplay),
          indexed: true,
        }
      : {
          block_burned_oru: feeBurnRow ? feeBurnRow.burned_oru.toString() : null,
          cumulative_burned_oru: null,
          block_burned_oru_display: feeBurnRow ? atomsToOruDisplay(feeBurnRow.burned_oru) : null,
          cumulative_burned_oru_display: null,
          equivalence_claimed: false as const,
          indexed: false,
        };
  const summary = {
    height: block.height,
    hash: block.block_hash,
    previous_block_hash,
    consensus_fingerprint: fp,
    consensus_fingerprint_display: displayFingerprint(fp),
    consensus_fingerprint_status: fingerprintStatus(fp),
    time: block.time,
    coinbase_txid: coinbaseTx?.txid ?? null,
    oru_subsidy_atoms: oruSubsidyAtoms.toString(),
    oru_subsidy_display: formatOruAtoms(oruSubsidyAtoms),
    rigor_res_minted: resMinted,
    res_minted_display,
    res_destroy_cost_display,
    bind_height: block.bind_height ?? null,
    bind_height_display: displayOrNotAvailable(block.bind_height),
    bind_hash_display: displayOrNotAvailable(block.bind_hash),
    res_probability_target: block.res_probability_target ?? null,
    res_probability_target_display: displayOrNA(block.res_probability_target),
  };

  return {
    block: {
      ...block,
      previous_block_hash,
      consensus_fingerprint: fp,
      consensus_fingerprint_display: displayFingerprint(fp),
      consensus_fingerprint_status: fingerprintStatus(fp),
      bind_height_display: summary.bind_height_display,
      bind_hash_display: summary.bind_hash_display,
      res_probability_target_display: summary.res_probability_target_display,
    },
    summary,
    rfc018_fee_burn,
    txs: enrichedTxs,
    convergence: winnerRow,
    convergence_panel,
    lineage_graph,
  };
}

export async function getTxByTxid(pool: Pool, txid: string): Promise<any | null> {
  const tx = await pool.query(`SELECT * FROM transactions WHERE txid = $1`, [txid]);
  if (!tx.rowCount) return null;

  const vin = await pool.query(
    `
      SELECT vin_index, prev_txid, prev_vout, sequence, coinbase, txinwitness, script_sig
      FROM inputs
      WHERE txid = $1
      ORDER BY vin_index ASC
    `,
    [txid],
  );

  const vout = await pool.query(
    `
      SELECT vout_index, value_atoms, asset_kind, address, witness_version, program_hex, script_pubkey
      FROM outputs
      WHERE txid = $1
      ORDER BY vout_index ASC
    `,
    [txid],
  );

  const resMint = await pool.query(
    `
      SELECT *
      FROM res_mints
      WHERE mint_txid = $1
      ORDER BY mint_vout_index ASC
    `,
    [txid],
  );

  const convergence = await pool.query(
    `
      SELECT ce.*,
             ${CONVERGENCE_IS_MINT_SQL} AS is_winner
      FROM convergence_events ce
      JOIN blocks b ON b.block_hash = ce.block_hash
      WHERE ce.convergence_txid = $1
      ORDER BY ce.block_height DESC, ce.convergence_txid ASC
      LIMIT 20
    `,
    [txid],
  );

  const blockCtx = await pool.query(
    `
      SELECT b.block_hash, b.height, b.rigor_res_minted, b.rigor_winner, b.committed_root
      FROM transactions t
      JOIN blocks b ON b.block_hash = t.block_hash
      WHERE t.txid = $1
      LIMIT 1
    `,
    [txid],
  );

  const inclusion = blockCtx.rows[0]
    ? { block_hash: blockCtx.rows[0].block_hash, block_height: blockCtx.rows[0].height }
    : null;
  const convergence_panel = buildTxConvergencePanel(convergence.rows, inclusion, {
    isResMint: (resMint.rowCount ?? 0) > 0,
  });

  const witness_panel = buildTxWitnessPanel(
    txid,
    vin.rows,
    resMint.rows,
    blockCtx.rows[0] ?? null,
    convergence_panel,
  );

  const linkedMint = await pool.query(
    `
      SELECT *
      FROM res_mints
      WHERE winner_convergence_txid = $1
      ORDER BY block_height DESC, mint_vout_index ASC
      LIMIT 5
    `,
    [txid],
  );

  const tipRow = await pool.query(`SELECT last_height FROM chain_state ORDER BY updated_at DESC LIMIT 1`);
  const indexerTipHeight = tipRow.rows[0] ? Number(tipRow.rows[0].last_height) : -1;

  const converge_proof_panel = buildConvergeProofPanel({
    txid,
    convergencePanel: convergence_panel,
    convergenceRows: convergence.rows,
    resMintRows: resMint.rows,
    linkedMintRows: linkedMint.rows,
    witnessPanel: witness_panel,
    vouts: vout.rows,
    block: blockCtx.rows[0] ?? null,
    indexerTipHeight,
  });

  const lineage_graph = await getLineageForTxid(pool, txid);

  const isCoinbase = Boolean(tx.rows[0].is_coinbase);
  let oruIn: bigint | null = 0n;
  let involvesRes = vout.rows.some((o) => o.asset_kind === "RES") || resMint.rows.length > 0;
  const enrichedVin: any[] = [];
  for (const input of vin.rows) {
    let prevAddress: string | null = null;
    let prevAsset: string | null = null;
    let prevAtoms: string | null = null;
    if (input.prev_txid != null && input.prev_vout != null) {
      const prev = await pool.query(
        `SELECT address, asset_kind, value_atoms FROM outputs WHERE txid = $1 AND vout_index = $2`,
        [input.prev_txid, input.prev_vout],
      );
      const row = prev.rows[0];
      if (!row) {
        oruIn = null;
      } else {
        prevAddress = row.address ?? null;
        prevAsset = row.asset_kind;
        prevAtoms = String(row.value_atoms);
        if (row.asset_kind === "RES") involvesRes = true;
        if (row.asset_kind === "ORU" && oruIn !== null) oruIn += BigInt(row.value_atoms);
      }
    }
    enrichedVin.push({ ...input, prev_address: prevAddress, prev_asset_kind: prevAsset, prev_value_atoms: prevAtoms });
  }

  const oruOut = vout.rows
    .filter((o) => o.asset_kind === "ORU")
    .reduce((acc, o) => acc + BigInt(o.value_atoms), 0n);
  const fee = computeTxFee({
    isCoinbase,
    oruInputAtoms: isCoinbase ? 0n : oruIn,
    oruOutputAtoms: oruOut,
    involvesRes: isCoinbase ? false : involvesRes,
  });

  const primary = pickPrimaryOruOutput(vout.rows);
  const hasResInput = enrichedVin.some(
    (v) => v.prev_asset_kind === "RES" && v.prev_value_atoms != null && BigInt(v.prev_value_atoms) > 0n,
  );
  const resSenderAddresses = [
    ...new Set(
      enrichedVin
        .filter((v) => v.prev_asset_kind === "RES" && v.prev_address)
        .map((v) => String(v.prev_address)),
    ),
  ];
  const resSenderMissing = enrichedVin.some((v) => v.prev_asset_kind === "RES" && !v.prev_address);
  const firstSender = enrichedVin.find((v) => v.prev_address)?.prev_address ?? (isCoinbase ? "Coinbase" : null);
  const primaryAtoms = primary ? BigInt(primary.output.value_atoms) : 0n;
  const hasResOut = vout.rows.some((o) => o.asset_kind === "RES" && BigInt(o.value_atoms) > 0n);
  const tx_kind = classifyTxKind({
    isCoinbase,
    primaryRole: primary?.role ?? null,
    hasOru: vout.rows.some((o) => o.asset_kind === "ORU"),
    hasRes: hasResOut,
    hasResInput,
    isResMint: resMint.rows.length > 0,
  });
  const resRecipients = [
    ...new Set(
      vout.rows
        .filter((o) => o.asset_kind === "RES" && BigInt(o.value_atoms) > 0n && o.address)
        .map((o) => String(o.address)),
    ),
  ];
  const resOutAtoms = vout.rows
    .filter((o) => o.asset_kind === "RES")
    .reduce((acc, o) => acc + BigInt(o.value_atoms), 0n);
  const transferSender =
    tx_kind === "res_transfer"
      ? resSenderAddresses.length === 1 && !resSenderMissing
        ? resSenderAddresses[0]
        : resSenderAddresses.length > 1 || resSenderMissing
          ? "Multiple inputs"
          : "Unavailable"
      : isCoinbase
        ? "Coinbase"
        : firstSender;
  const transferReceiver = tx_kind === "res_transfer" ? (resRecipients.length === 1 ? resRecipients[0] : null) : primary?.output.address ?? null;

  const resAmountDisplay = atomsToResDisplay(resOutAtoms);
  const summary = {
    txid,
    is_coinbase: isCoinbase,
    sender: transferSender,
    receiver: transferReceiver,
    amount_atoms: tx_kind === "res_transfer" ? resOutAtoms.toString() : primary ? primaryAtoms.toString() : oruOut.toString(),
    amount_display: tx_kind === "res_transfer" ? resAmountDisplay : formatOruAtoms(primary ? primaryAtoms : oruOut),
    amount_role: tx_kind === "res_transfer" ? "output" : primary?.role ?? null,
    amount_total_atoms: tx_kind === "res_transfer" ? resOutAtoms.toString() : oruOut.toString(),
    amount_total_display: tx_kind === "res_transfer" ? resAmountDisplay : formatOruAtoms(oruOut),
    tx_kind,
    tx_kind_label: txKindLabel(tx_kind),
    minted_res: tx_kind === "res_mint",
    kind_notice: txKindNotice(tx_kind),
    converge_output_note: convergeOutputNote(tx_kind),
    fee,
    subsidy_display: isCoinbase ? formatOruAtoms(oruOut) : null,
  };

  const enrichedVout = vout.rows.map((o) => {
    const is_convergence = isConvergenceScriptPubKey(String(o.script_pubkey ?? ""));
    return {
      ...o,
      is_convergence,
      script: is_convergence ? OP_CONVERGE_SCRIPT_NAME : null,
      script_state: is_convergence ? CONVERGENCE_OUTPUT_STATE : null,
    };
  });

  return {
    tx: tx.rows[0],
    vin: enrichedVin,
    vout: enrichedVout,
    summary,
    resMint: resMint.rows,
    convergence: convergence.rows,
    convergence_panel,
    witness_panel,
    converge_proof_panel,
    lineage_graph,
  };
}

export async function listResMints(pool: Pool, limit: number): Promise<any[]> {
  const res = await pool.query(
    `
      SELECT rm.block_height, b.block_hash, rm.mint_txid, rm.mint_vout_index, rm.mint_amount,
             rm.winner_convergence_txid,
             rm.bind_hash, rm.bind_height,
             rm.draw_eligible,
             rm.res_probability_target,
             rm.committed_root, rm.computed_root,
             rm.proof_items
      FROM res_mints rm
      JOIN blocks b ON b.block_hash = rm.block_hash
      ORDER BY rm.block_height DESC
      LIMIT $1
    `,
    [limit],
  );
  return res.rows;
}

export function formatResEventRow(row: {
  convergence_txid: string;
  included_height: string | null;
  last_window_height?: string | null;
  mint_height?: string | null;
  lock_atoms?: string | null;
  mint_amount_atoms?: string | null;
}): Record<string, unknown> {
  const mintAtoms = String(row.mint_amount_atoms ?? "0");
  const minted = BigInt(mintAtoms) > 0n;
  const height = row.mint_height ?? row.included_height;
  return {
    convergence_txid: row.convergence_txid,
    included_height: row.included_height,
    last_window_height: row.last_window_height ?? row.included_height,
    award_height: height,
    mint_height: minted ? (row.mint_height ?? row.included_height) : null,
    height,
    draw_eligible: minted,
    lock_display: atomsToOruDisplay(String(row.lock_atoms ?? "0")),
    mint_amount: minted ? atomsToResDisplay(mintAtoms) : "0",
    result: minted ? `Minted ${atomsToResDisplay(mintAtoms)}` : "Destroy only",
  };
}

export async function listResEvents(pool: Pool, limit: number, eligibleOnly: boolean): Promise<any[]> {
  const res = await pool.query(
    `
      SELECT
        burn.txid AS convergence_txid,
        burn.block_height::text AS included_height,
        burn.block_height::text AS last_window_height,
        burn.lock_atoms::text AS lock_atoms,
        COALESCE(mint.mint_amount_atoms, 0)::text AS mint_amount_atoms,
        mint.block_height::text AS mint_height
      FROM (
        SELECT o.txid, t.block_height, SUM(o.value_atoms) AS lock_atoms
          FROM outputs o
          JOIN transactions t ON t.txid = o.txid
         WHERE o.asset_kind = 'ORU'
           AND ${opConvergeScriptSql("o.script_pubkey")}
           AND o.value_atoms > 0
         GROUP BY o.txid, t.block_height
      ) burn
      LEFT JOIN (
        SELECT mint_txid, SUM(mint_amount) AS mint_amount_atoms, MIN(block_height) AS block_height
          FROM res_mints
         GROUP BY mint_txid
      ) mint ON mint.mint_txid = burn.txid
      WHERE ($2::boolean = FALSE) OR COALESCE(mint.mint_amount_atoms, 0) > 0
      ORDER BY burn.block_height DESC, burn.txid ASC
      LIMIT $1
    `,
    [limit, eligibleOnly],
  );
  return res.rows.map((row) => formatResEventRow(row));
}

/** @deprecated Use getAddressIntelligence from addressDb.ts */
export async function getAddressBalances(pool: Pool, address: string): Promise<any> {
  const { getAddressIntelligence } = await import("./addressDb");
  const { loadConfigFromEnv } = await import("./config");
  const row = await getAddressIntelligence(pool, loadConfigFromEnv(), address, 1, 50);
  if (!row) return { oru_utxos: [], res_outputs: [] };
  return row;
}

