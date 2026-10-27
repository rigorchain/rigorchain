import type { Config } from "./config";
import type { Pool } from "pg";
import {
  clearImporterRecovery,
  getImporterRecoveryState,
  recordImporterFailure,
  recordRecoveryRetry,
  shouldRetryImport,
} from "./importerRecovery";
import { logMismatchEvent } from "./mismatchLog";
import { rpc } from "./rpcCli";
import { classifyScriptPubKey, isConvergenceScriptPubKey, type AssetKind } from "./script";
import { oruStringToAtoms, resStringToAtoms } from "./amount";
import { findBodyResMints, rfc020MintProofItems } from "./resMint";
import { collectConsensusFingerprint, winnerSigmaFromCandidates } from "./consensusFingerprint";
import {
  EMPTY_DRAW_ELIGIBLE_ROOT,
  isA8PayRootMismatch,
  optionalBigInt,
  optionalHash,
  optionalString,
} from "./rpcFields";
import { upsertRfc018FeeBurnStats, backfillMissingRfc018FeeBurn } from "./rfc018FeeBurnStore";
import { withDbTransaction } from "./dbTransaction";
import { rfc018DestroyedFeeOru, type IndexedPrevout } from "./rfc018FeeBurn";

type TxVinWitness = string[] | null;
type OutputMeta = {
  vout_index: number;
  value_atoms: bigint;
  asset_kind: AssetKind;
  script_pubkey: string;
  address: string | null;
  witness_version: number | null;
  program_hex: string | null;
};

type TxVinMeta = {
  vin_index: number;
  prev_txid: string | null;
  prev_vout: number | null;
  sequence: bigint | null;
  coinbase: boolean;
  witness: TxVinWitness; // txinwitness or final_scriptwitness
  script_sig: any;
};

type ParsedTx = {
  txid: string;
  wtxid: string | null;
  is_coinbase: boolean;
  version: number | null;
  locktime: bigint | null;
  block_tx_index: number;
  vins: TxVinMeta[];
  vouts: OutputMeta[];
};

function asString(x: unknown): string {
  if (x === null || x === undefined) return "";
  return typeof x === "string" ? x : String(x);
}

/** node-pg sends JS arrays as PG array literals; JSONB columns need JSON text. */
function jsonbParam(value: unknown): string | null {
  if (value === null || value === undefined) return null;
  return JSON.stringify(value);
}

function extractWitnessStack(vin: any): string[] | null {
  // Core RPC naming varies with verbosity and decoding path.
  if (Array.isArray(vin?.txinwitness)) return vin.txinwitness as string[];
  if (Array.isArray(vin?.final_scriptwitness)) return vin.final_scriptwitness as string[];
  return null;
}

export async function importerTick(
  config: Config,
  pool: Pool,
  nowTag: string,
): Promise<{ imported: number; reorgHappened: boolean; stalled: boolean }> {
  // Single-thread determinism: importerTick must be serialized by caller.
  const chain = await (await import("./db")).getOrCreateChainState(pool, config);
  const recovery = getImporterRecoveryState();

  if (recovery.stalled_height != null && !shouldRetryImport(Date.now(), config.mismatchRetryIntervalMs)) {
    return { imported: 0, reorgHappened: false, stalled: true };
  }

  if (recovery.stalled_height != null) {
    recordRecoveryRetry(recovery.stalled_height);
  }

  // Tip tracking: use getblockchaininfo as stable contract surface.
  const info = await rpc.getblockchaininfo(config);
  const tipHeight = Number(info.blocks);
  const nodeBestHash = typeof info.bestblockhash === "string" ? info.bestblockhash : null;

  const toBigInt = (v: unknown): bigint => {
    if (typeof v === "bigint") return v;
    if (typeof v === "string") return BigInt(v);
    if (typeof v === "number") return BigInt(v);
    return BigInt(0);
  };

  let nodeLastHashAtChainHeight: string | null = null;
  const chainLastHeight = toBigInt(chain.last_height);
  if (chainLastHeight >= 0n) {
    nodeLastHashAtChainHeight = await rpc.getblockhash(config, chainLastHeight.toString());
  }

  // Reorg detection based on same-height hash mismatch.
  let reorgHappened = false;
  let nextStartHeight = Number(chainLastHeight + 1n);

  const chainLastHash = chain.last_block_hash;
  if (chainLastHeight >= 0n && chainLastHash && nodeLastHashAtChainHeight && nodeLastHashAtChainHeight !== chainLastHash) {
    reorgHappened = true;

    // Find fork height by walking back deterministically (bounded by maxSupportedReorgDepth).
    let h = Number(chainLastHeight);
    let forkHeight: number | null = null;
    for (let i = 0; i <= config.maxSupportedReorgDepth; i++) {
      const nodeHash = await rpc.getblockhash(config, String(h));
      const dbRows = await pool.query<{ block_hash: string }>(
        `SELECT block_hash FROM blocks WHERE height = $1 LIMIT 1`,
        [BigInt(h)],
      );
      const dbHash = dbRows.rows[0]?.block_hash ?? null;
      if (dbHash && dbHash === nodeHash) {
        forkHeight = h;
        break;
      }
      h = h - 1;
      if (h < 0) break;
    }

    if (forkHeight === null) {
      // Full reindex trigger (depth too large or DB missing).
      await (await import("./db")).truncateFromHeight(pool, 0n);
      await (await import("./db")).updateChainState(pool, config, { last_height: -1n, last_block_hash: null });
      nextStartHeight = 0;
    } else {
      const truncateFrom = Math.max(0, forkHeight - config.reorgSafetyWindow);
      await (await import("./db")).truncateFromHeight(pool, BigInt(truncateFrom));
      await (await import("./db")).updateChainState(pool, config, { last_height: BigInt(truncateFrom - 1), last_block_hash: null });
      nextStartHeight = truncateFrom;
    }
  }

  const endHeight = Math.min(tipHeight, nextStartHeight + config.importBatchMaxHeight - 1);
  const catchingUp = endHeight < tipHeight;
  if (nextStartHeight > endHeight) {
  await backfillMissingCRoot(config, pool);
  await backfillMissingFingerprint(config, pool);
  await backfillMissingResMints(config, pool);
  await backfillA8PayRootFlags(pool, config);
  await backfillMissingRfc018FeeBurn(pool);
  return { imported: 0, reorgHappened, stalled: recovery.stalled_height != null };
  }

  let imported = 0;
  for (let h = nextStartHeight; h <= endHeight; h++) {
    try {
      const res = await importBlockAtHeight(config, pool, h);
      await (await import("./db")).updateChainState(pool, config, {
        last_height: BigInt(h),
        last_block_hash: res.blockHash,
      });
      imported++;
      clearImporterRecovery(h);
      if (!res.mismatch) {
        await (await import("./db")).updateChainState(pool, config, { critical_root_mismatch: false });
      }
    } catch (e) {
      const msg = String(e);
      recordImporterFailure(h, msg);
      if (config.stopOnRootMismatch) {
        return { imported, reorgHappened, stalled: true };
      }
      throw e;
    }
  }

  if (!catchingUp) {
    await backfillMissingCRoot(config, pool);
    await backfillMissingFingerprint(config, pool);
    await backfillMissingResMints(config, pool);
    await backfillA8PayRootFlags(pool, config);
    await backfillMissingRfc018FeeBurn(pool);
  }
  return { imported, reorgHappened, stalled: false };
}

/** Lottery C-root backfill is retired with getconvergenceresult. */
async function backfillMissingCRoot(_config: Config, _pool: Pool): Promise<void> {
  return;
}

/** Fill identity fingerprint on already-imported blocks. Does not recompute consensus. */
async function backfillMissingFingerprint(config: Config, pool: Pool): Promise<void> {
  const missing = await pool.query<{ block_hash: string; height: string }>(
    `SELECT block_hash, height::text AS height FROM blocks WHERE consensus_fingerprint IS NULL ORDER BY height DESC LIMIT 16`,
  );
  for (const row of missing.rows) {
    try {
      const height = Number(row.height);
      const block = await rpc.getblock(config, row.block_hash, 1);
      const cr: Record<string, unknown> = {};
      const winner = optionalHash(block.rigor_winner) ?? optionalHash(cr.draw_winner_txid) ?? "";
      const sigma = winnerSigmaFromCandidates(winner, cr.candidates);
      const parentBits = await readParentBits(config, pool, height, optionalString(block.previousblockhash));
      const fp = collectConsensusFingerprint({
        published: block.consensus_fingerprint ?? cr.consensus_fingerprint,
        convergenceRootRpc: cr.convergence_root,
        winnerTxidRpc: winner,
        sigmaRpc: sigma,
        height,
        targetPrevBits: parentBits,
      });
      if (!fp) continue;
      await pool.query(
        `UPDATE blocks SET consensus_fingerprint = $1 WHERE block_hash = $2 AND consensus_fingerprint IS NULL`,
        [fp, row.block_hash],
      );
    } catch {
      /* next height */
    }
  }
}

/** RFC-020: mint is in the confirming body tx. Re-import blocks that never got a res_mints row. */
async function backfillMissingResMints(config: Config, pool: Pool): Promise<void> {
  const missing = await pool.query<{ height: string }>(
    `
      SELECT height::text AS height
      FROM blocks
      WHERE rigor_res_minted = 1
        AND NOT EXISTS (
          SELECT 1 FROM res_mints rm WHERE rm.block_hash = blocks.block_hash
        )
      ORDER BY height ASC
      LIMIT 8
    `,
  );
  for (const row of missing.rows) {
    try {
      await importBlockAtHeight(config, pool, Number(row.height));
    } catch {
      /* next height */
    }
  }
}

/**
 * Rewrite blocks.mismatch to the A-8 pay-link (this.committed vs parent.computed).
 * Genesis computed is uint256(); height 1 commits the empty eligible-set root — not a mismatch.
 */
async function backfillA8PayRootFlags(pool: Pool, config: Config): Promise<void> {
  const chain = await (await import("./db")).getOrCreateChainState(pool, config);
  const before = await pool.query<{ n: string }>(`SELECT COUNT(*)::text AS n FROM blocks WHERE mismatch = TRUE`);
  const nBefore = Number(before.rows[0]?.n ?? "0");
  if (nBefore === 0 && !chain.critical_root_mismatch) return;

  if (nBefore > 0) {
    await pool.query(`UPDATE blocks SET mismatch = FALSE WHERE height <= 0 AND mismatch = TRUE`);
    await pool.query(
      `
        UPDATE blocks AS b
        SET mismatch = (
          lower(b.committed_root) IS DISTINCT FROM
            CASE
              WHEN p.computed_root IS NULL OR p.computed_root = '' THEN lower(b.committed_root)
              WHEN p.computed_root ~ '^[0]+$' THEN $1
              ELSE lower(p.computed_root)
            END
        )
        FROM blocks AS p
        WHERE p.height = b.height - 1
          AND b.height > 0
      `,
      [EMPTY_DRAW_ELIGIBLE_ROOT],
    );
  }
  const remaining = await pool.query<{ n: string }>(`SELECT COUNT(*)::text AS n FROM blocks WHERE mismatch = TRUE`);
  const any = Number(remaining.rows[0]?.n ?? "0") > 0;
  if (any !== Boolean(chain.critical_root_mismatch)) {
    await (await import("./db")).updateChainState(pool, config, { critical_root_mismatch: any });
  }
}

async function readParentBits(
  config: Config,
  pool: Pool,
  height: number,
  previousblockhash: string | null,
): Promise<string | null> {
  if (height <= 0) return null;
  if (previousblockhash) {
    const cached = await pool.query<{ bits: string }>(
      `SELECT bits FROM blocks WHERE block_hash = $1 LIMIT 1`,
      [previousblockhash],
    );
    if (cached.rows[0]?.bits) return String(cached.rows[0].bits);
    try {
      const prev = await rpc.getblock(config, previousblockhash, 1);
      const bits = asString(prev?.bits);
      return bits || null;
    } catch {
      return null;
    }
  }
  const byHeight = await pool.query<{ bits: string }>(
    `SELECT bits FROM blocks WHERE height = $1 LIMIT 1`,
    [BigInt(height - 1)],
  );
  return byHeight.rows[0]?.bits ? String(byHeight.rows[0].bits) : null;
}

async function importBlockAtHeight(
  config: Config,
  pool: Pool,
  height: number,
): Promise<{ blockHash: string; mismatch: boolean }> {
  const blockHash = await rpc.getblockhash(config, String(height));
  // Verbosity 2 embeds full txs (genesis coinbase is not available via getrawtransaction).
  const block = await rpc.getblock(config, blockHash, 2);
  const cr: Record<string, unknown> = {};

  // RES target is RPC-authoritative. Missing → store null; UI shows N/A. Never crash.
  const resProbTarget =
    optionalString(block.res_probability_target) ?? optionalString(cr.res_probability_target);

  const committed = asString(cr.draw_candidate_root_committed);
  const computed = asString(cr.draw_candidate_root_computed);

  // A-8: committed = draw this block PAYS (parent award); computed = draw this block AWARDS.
  // Same-block inequality on award/payout heights is expected. Critical check is
  // this.committed === parent.computed.
  const prevHash = optionalString(block.previousblockhash);
  let parentComputed: string | null = null;
  if (height > 0 && prevHash) {
    const dbParent = await pool.query<{ computed_root: string | null }>(
      `SELECT computed_root FROM blocks WHERE block_hash = $1 LIMIT 1`,
      [prevHash],
    );
    parentComputed = optionalString(dbParent.rows[0]?.computed_root);
  }

  const mismatch = isA8PayRootMismatch(committed, parentComputed, height);
  if (mismatch) {
    await (await import("./db")).updateChainState(pool, config, { critical_root_mismatch: true });
    logMismatchEvent({
      kind: "root_committed_computed",
      height,
      block_hash: blockHash,
      committed_root: committed,
      computed_root: parentComputed ?? computed,
      message: "A-8 pay-link mismatch: this block's draw_candidate_root_committed != parent draw_candidate_root_computed",
    });
    if (config.stopOnRootMismatch) throw new Error("root mismatch committed != computed");
  } else {
    await (await import("./db")).updateChainState(pool, config, { critical_root_mismatch: false });
  }

  const blockTxEntries: unknown[] = Array.isArray(block.tx) ? block.tx : [];
  const txids: string[] = blockTxEntries.map((t) => (typeof t === "string" ? t : asString((t as { txid?: unknown }).txid)));
  const txCount = txids.length;

  const blockInsert = {
    block_hash: blockHash,
    height: BigInt(block.height ?? height),
    time: BigInt(block.time),
    bits: asString(block.bits),
    res_probability_target: resProbTarget,
    merkleroot: asString(block.merkleroot),
    draw_candidate_root: asString(block.draw_candidate_root ?? block.draw_candidate_root_committed ?? committed),
    rigor_res_minted: Number(block.rigor_res_minted ?? 0),
    rigor_winner: optionalHash(block.rigor_winner) ?? optionalHash(cr.draw_winner_txid) ?? "",
    cumulative_convergence: block.cumulative_convergence != null ? asString(block.cumulative_convergence) : null,
    opus_hash:
      block.pow_hash != null
        ? asString(block.pow_hash)
        : block.opus_hash != null
          ? asString(block.opus_hash)
          : null,
    tx_count: txCount,

    bind_hash: optionalHash(cr.bind_hash),
    bind_height: optionalBigInt(cr.bind_height),
    committed_root: committed,
    computed_root: computed,
    mismatch,
    c_root: asString(cr.convergence_root),
    consensus_fingerprint: collectConsensusFingerprint({
      published: block.consensus_fingerprint ?? cr.consensus_fingerprint,
      convergenceRootRpc: cr.convergence_root,
      winnerTxidRpc: optionalHash(block.rigor_winner) ?? optionalHash(cr.draw_winner_txid),
      sigmaRpc: winnerSigmaFromCandidates(
        optionalHash(block.rigor_winner) ?? optionalHash(cr.draw_winner_txid) ?? "",
        cr.candidates,
      ),
      height: Number(block.height ?? height),
      targetPrevBits: await readParentBits(config, pool, height, optionalString(block.previousblockhash)),
    }),
  };

  // pool.query() checks out a different session per call. BEGIN on one
  // connection does not cover inserts on another, and ROLLBACK then deletes
  // only the rows that shared the BEGIN session. Height 1399 committed the
  // witness output and lost coinbase vout 0 that way.
  return withDbTransaction(pool, async (client) => {
    await client.query(
      `
        INSERT INTO blocks(
          block_hash, height, time, bits, res_probability_target,
          merkleroot, draw_candidate_root,
          rigor_res_minted, rigor_winner,
          cumulative_convergence, opus_hash, tx_count,
          bind_hash, bind_height, committed_root, computed_root, mismatch,
          c_root, consensus_fingerprint
        )
        VALUES(
          $1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19
        )
        ON CONFLICT (block_hash) DO UPDATE SET
          height = EXCLUDED.height,
          time = EXCLUDED.time,
          bits = EXCLUDED.bits,
          res_probability_target = EXCLUDED.res_probability_target,
          merkleroot = EXCLUDED.merkleroot,
          draw_candidate_root = EXCLUDED.draw_candidate_root,
          rigor_res_minted = EXCLUDED.rigor_res_minted,
          rigor_winner = EXCLUDED.rigor_winner,
          cumulative_convergence = EXCLUDED.cumulative_convergence,
          opus_hash = EXCLUDED.opus_hash,
          tx_count = EXCLUDED.tx_count,
          bind_hash = EXCLUDED.bind_hash,
          bind_height = EXCLUDED.bind_height,
          committed_root = EXCLUDED.committed_root,
          computed_root = EXCLUDED.computed_root,
          mismatch = EXCLUDED.mismatch,
          c_root = EXCLUDED.c_root,
          consensus_fingerprint = EXCLUDED.consensus_fingerprint
      `,
      [
        blockInsert.block_hash,
        blockInsert.height,
        blockInsert.time,
        blockInsert.bits,
        blockInsert.res_probability_target,
        blockInsert.merkleroot,
        blockInsert.draw_candidate_root,
        blockInsert.rigor_res_minted,
        blockInsert.rigor_winner,
        blockInsert.cumulative_convergence,
        blockInsert.opus_hash,
        blockInsert.tx_count,
        blockInsert.bind_hash,
        blockInsert.bind_height,
        blockInsert.committed_root,
        blockInsert.computed_root,
        blockInsert.mismatch,
        blockInsert.c_root,
        blockInsert.consensus_fingerprint,
      ],
    );

    const parsedTxs: ParsedTx[] = [];
    const mintTxids = new Set<string>();

    const candidates: any[] = Array.isArray(cr.candidates) ? cr.candidates : [];
    const candidateByTxid = new Map<string, any>();
    for (const c of candidates) {
      candidateByTxid.set(asString(c.txid), c);
    }

    // Pass 1: parse transactions and insert outputs (and oru_utxos rows).
    for (let i = 0; i < blockTxEntries.length; i++) {
      const entry = blockTxEntries[i];
      const txid = txids[i];
      const tx =
        typeof entry === "string"
          ? await rpc.getrawtransaction(config, entry, true)
          : (entry as Record<string, unknown>);

      const wtxid = tx?.hash != null ? asString(tx.hash) : null;
      const isCoinbase = Array.isArray(tx?.vin) && tx.vin.some((v: any) => v && typeof v.coinbase === "string");
      const version = tx?.version != null ? Number(tx.version) : null;
      const locktime = tx?.locktime != null ? BigInt(tx.locktime) : null;

      // FK correctness: outputs/input rows reference transactions(txid).
      await client.query(
        `
          INSERT INTO transactions(txid, wtxid, block_hash, block_height, block_tx_index, version, locktime, is_coinbase, raw_hex)
          VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9)
          ON CONFLICT (txid) DO UPDATE SET
            wtxid = EXCLUDED.wtxid,
            block_hash = EXCLUDED.block_hash,
            block_height = EXCLUDED.block_height,
            block_tx_index = EXCLUDED.block_tx_index,
            version = EXCLUDED.version,
            locktime = EXCLUDED.locktime,
            is_coinbase = EXCLUDED.is_coinbase,
            raw_hex = EXCLUDED.raw_hex
        `,
        [
          txid,
          wtxid,
          blockHash,
          BigInt(height),
          i,
          version,
          locktime,
          isCoinbase,
          tx?.hex ?? null,
        ],
      );

      const vins: TxVinMeta[] = [];
      for (let vin_index = 0; vin_index < (tx?.vin?.length ?? 0); vin_index++) {
        const vin = tx.vin[vin_index];
        const prev_txid = vin?.txid != null ? asString(vin.txid) : null;
        const prev_vout = vin?.vout != null ? Number(vin.vout) : null;
        const sequence = vin?.sequence != null ? BigInt(vin.sequence) : null;
        const coinbase = vin?.coinbase != null;
        const witnessStack = extractWitnessStack(vin);
        const script_sig = vin?.scriptSig ?? null;

        vins.push({
          vin_index,
          prev_txid,
          prev_vout,
          sequence,
          coinbase: !!coinbase,
          witness: witnessStack,
          script_sig,
        });
      }

      const vouts: OutputMeta[] = [];
      for (let vout_index = 0; vout_index < (tx?.vout?.length ?? 0); vout_index++) {
        const v = tx.vout[vout_index];
        const scriptPubKeyHex = v?.scriptPubKey?.hex != null ? asString(v.scriptPubKey.hex) : asString(v?.scriptPubKey);
        const cls = classifyScriptPubKey(scriptPubKeyHex, config.bech32Hrp);
        const asset_kind = cls.assetKind;

        const rpcValue = v?.value != null ? asString(v.value) : "0";

        let value_atoms: bigint = 0n;
        if (asset_kind === "ORU") value_atoms = oruStringToAtoms(rpcValue);
        else if (asset_kind === "RES") value_atoms = resStringToAtoms(rpcValue);
        else value_atoms = 0n;

        const outMeta: OutputMeta = {
          vout_index,
          value_atoms,
          asset_kind,
          script_pubkey: scriptPubKeyHex,
          address: cls.address,
          witness_version: cls.witnessVersion,
          program_hex: cls.programHex,
        };
        vouts.push(outMeta);

        await client.query(
          `
            INSERT INTO outputs(txid, vout_index, value_atoms, asset_kind, script_pubkey, address, witness_version, program_hex)
            VALUES($1,$2,$3,$4,$5,$6,$7,$8)
            ON CONFLICT (txid, vout_index) DO UPDATE SET
              value_atoms = EXCLUDED.value_atoms,
              asset_kind = EXCLUDED.asset_kind,
              script_pubkey = EXCLUDED.script_pubkey,
              address = EXCLUDED.address,
              witness_version = EXCLUDED.witness_version,
              program_hex = EXCLUDED.program_hex
          `,
          [
            txid,
            vout_index,
            value_atoms,
            asset_kind,
            scriptPubKeyHex,
            cls.address,
            cls.witnessVersion,
            cls.programHex,
          ],
        );

        // Circulating ORU UTXO index: skip OP_CONVERGE. Node AddCoins also omits IsConvergenceScript.
        if (asset_kind === "ORU" && !isConvergenceScriptPubKey(scriptPubKeyHex)) {
          await client.query(
            `
              INSERT INTO oru_utxos(txid, vout_index, height, amount_atoms, script_pubkey, address, spent, spending_txid, spending_vin_index)
              VALUES($1,$2,$3,$4,$5,$6,FALSE,NULL,NULL)
              ON CONFLICT (txid, vout_index) DO UPDATE SET
                height = EXCLUDED.height,
                amount_atoms = EXCLUDED.amount_atoms,
                script_pubkey = EXCLUDED.script_pubkey,
                address = EXCLUDED.address,
                spent = oru_utxos.spent,
                spending_txid = oru_utxos.spending_txid,
                spending_vin_index = oru_utxos.spending_vin_index
            `,
            [txid, vout_index, BigInt(height), value_atoms, scriptPubKeyHex, cls.address],
          );
        }
      }

      parsedTxs.push({
        txid,
        wtxid,
        is_coinbase: isCoinbase,
        version,
        locktime,
        block_tx_index: i,
        vins,
        vouts,
      });
    }

    const bodyMints = findBodyResMints(parsedTxs);
    for (const mint of bodyMints) mintTxids.add(mint.mintTxid);
    if (bodyMints.length === 0 && blockInsert.rigor_res_minted === 1) {
      logMismatchEvent({
        kind: "res_mint_missing_body",
        height,
        block_hash: blockHash,
        message: "rigor_res_minted=1 but no RFC-020 body mint (OP_CONVERGE + RES) was indexed",
      });
    }

    for (const tx of parsedTxs) {
      if (tx.is_coinbase) continue;
      const hasBurn = tx.vouts.some(
        (o) => isConvergenceScriptPubKey(o.script_pubkey) && o.value_atoms > 0n,
      );
      if (!hasBurn || candidateByTxid.has(tx.txid)) continue;
      const bodyCand = { txid: tx.txid, draw_eligible: true, h: null, convergence_type: "rfc020_body" };
      candidates.push(bodyCand);
      candidateByTxid.set(tx.txid, bodyCand);
    }

    // Pass 2: insert convergence candidates (convergence_events).
    for (const c of candidates.slice().sort((a, b) => asString(a.txid).localeCompare(asString(b.txid)))) {
      const convTxid = asString(c.txid);
      const drawEligible = Boolean(c.draw_eligible);

      // A-8: this block's F is the next window. Do not attribute the payout to this block's winner.
      const mintAmountAtoms = 0n;

      await client.query(
        `
          INSERT INTO convergence_events(
            block_hash, block_height, convergence_txid,
            selection_digest_h, draw_eligible, convergence_type,
            bind_hash, bind_height,
            convergence_root, res_probability_target,
            committed_root, computed_root, mismatch,
              mint_amount
          )
          VALUES(
            $1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14
          )
          ON CONFLICT (block_hash, convergence_txid) DO UPDATE SET
            selection_digest_h = EXCLUDED.selection_digest_h,
            draw_eligible = EXCLUDED.draw_eligible,
            convergence_type = EXCLUDED.convergence_type,
            bind_hash = EXCLUDED.bind_hash,
            bind_height = EXCLUDED.bind_height,
            convergence_root = EXCLUDED.convergence_root,
            res_probability_target = EXCLUDED.res_probability_target,
            committed_root = EXCLUDED.committed_root,
            computed_root = EXCLUDED.computed_root,
            mismatch = EXCLUDED.mismatch,
              mint_amount = EXCLUDED.mint_amount
        `,
        [
          blockHash,
          BigInt(height),
          convTxid,
          asString(c.h ?? null),
          drawEligible,
          asString(c.convergence_type ?? null),
          optionalHash(cr.bind_hash),
          optionalBigInt(cr.bind_height),
          committed,
          optionalString(cr.res_probability_target) ?? resProbTarget,
          committed,
          computed,
          mismatch,
          mintAmountAtoms,
        ],
      );
    }

    // Pass 3: RFC-020 body-tx mints (same txid burns ORU and creates RES).
    for (const mint of bodyMints) {
      const proof_items_obj = rfc020MintProofItems({
        burnAtoms: mint.burnAtoms,
        mintAtoms: mint.amountAtoms,
        height,
      });
      for (const o of mint.outputs) {
        await client.query(
          `
            INSERT INTO res_mints(
              mint_txid, mint_vout_index,
              block_hash, block_height,
              winner_convergence_txid,
              mint_amount,
              bind_hash, bind_height,
              convergence_root,
              draw_eligible,
              res_probability_target,
              committed_root, computed_root,
              proof_items
            )
            VALUES(
              $1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14
            )
            ON CONFLICT (mint_txid, mint_vout_index) DO UPDATE SET
              block_hash = EXCLUDED.block_hash,
              block_height = EXCLUDED.block_height,
              winner_convergence_txid = EXCLUDED.winner_convergence_txid,
              mint_amount = EXCLUDED.mint_amount,
              bind_hash = EXCLUDED.bind_hash,
              bind_height = EXCLUDED.bind_height,
              convergence_root = EXCLUDED.convergence_root,
              draw_eligible = EXCLUDED.draw_eligible,
              res_probability_target = EXCLUDED.res_probability_target,
              committed_root = EXCLUDED.committed_root,
              computed_root = EXCLUDED.computed_root,
              proof_items = EXCLUDED.proof_items
          `,
          [
            mint.mintTxid,
            o.vout_index,
            blockHash,
            BigInt(height),
            mint.mintTxid,
            o.value_atoms,
            optionalHash(cr.bind_hash),
            optionalBigInt(cr.bind_height),
            committed,
            true,
            optionalString(cr.res_probability_target) ?? resProbTarget,
            committed,
            computed,
            jsonbParam(proof_items_obj),
          ],
        );
      }
      await client.query(
        `UPDATE convergence_events SET mint_amount = $1 WHERE convergence_txid = $2 AND block_height = $3`,
        [mint.amountAtoms, mint.mintTxid, BigInt(height)],
      );
    }

    // Pass 4: insert inputs; mark ORU UTXOs spent; insert RES transfer rows; RFC-018 burn stats.
    let blockRfc018Burn = 0n;
    for (const tx of parsedTxs) {
      // Determine first RES input (for deterministic res_transfer source binding).
      let firstResInput: { vin_index: number; prev_txid: string; prev_vout: number; } | null = null;
      const prevouts: Array<IndexedPrevout | null> = [];

      for (const vin of tx.vins) {
        await client.query(
          `
            INSERT INTO inputs(txid, vin_index, prev_txid, prev_vout, sequence, coinbase, txinwitness, script_sig)
            VALUES($1,$2,$3,$4,$5,$6,$7,$8)
            ON CONFLICT (txid, vin_index) DO UPDATE SET
              prev_txid = EXCLUDED.prev_txid,
              prev_vout = EXCLUDED.prev_vout,
              sequence = EXCLUDED.sequence,
              coinbase = EXCLUDED.coinbase,
              txinwitness = EXCLUDED.txinwitness,
              script_sig = EXCLUDED.script_sig
          `,
          [
            tx.txid,
            vin.vin_index,
            vin.prev_txid,
            vin.prev_vout,
            vin.sequence,
            vin.coinbase,
            jsonbParam(vin.witness),
            jsonbParam(vin.script_sig),
          ],
        );

        if (!vin.prev_txid || vin.prev_vout === null) {
          prevouts.push(null);
          continue;
        }
        const prev = await client.query<{ asset_kind: string; value_atoms: string }>(
          `SELECT asset_kind, value_atoms::text AS value_atoms FROM outputs WHERE txid = $1 AND vout_index = $2`,
          [vin.prev_txid, vin.prev_vout],
        );
        const prevAsset = prev.rows[0]?.asset_kind ?? null;
        if (prev.rows[0]) {
          const kind =
            prevAsset === "ORU" || prevAsset === "RES" || prevAsset === "INVALID"
              ? prevAsset
              : "NONE";
          prevouts.push({ asset_kind: kind, value_atoms: BigInt(prev.rows[0].value_atoms) });
        } else {
          prevouts.push(null);
        }

        if (prevAsset === "ORU") {
          await client.query(
            `
              UPDATE oru_utxos
              SET spent = TRUE,
                  spending_txid = $1,
                  spending_vin_index = $2
              WHERE txid = $3 AND vout_index = $4 AND spent = FALSE
            `,
            [tx.txid, vin.vin_index, vin.prev_txid, vin.prev_vout],
          );
        }

        if (prevAsset === "RES" && firstResInput === null) {
          firstResInput = { vin_index: vin.vin_index, prev_txid: vin.prev_txid, prev_vout: vin.prev_vout };
        }
      }

      blockRfc018Burn += rfc018DestroyedFeeOru({
        isCoinbase: tx.is_coinbase,
        isResMint: mintTxids.has(tx.txid),
        inputs: prevouts,
        outputs: tx.vouts,
      });

      // Insert transfer outputs if tx spends a RES input and is not the mint tx.
      if (!mintTxids.has(tx.txid) && firstResInput) {
        const resOutputs = tx.vouts.filter((o) => o.asset_kind === "RES");
        for (const o of resOutputs) {
          await client.query(
            `
              INSERT INTO res_transfers(
                spend_txid, spend_vin_index, spend_block_hash,
                res_input_txid, res_input_vout_index,
                res_output_txid, res_output_vout_index,
                res_amount_atoms
              )
              VALUES($1,$2,$3,$4,$5,$6,$7,$8)
              ON CONFLICT (spend_txid, spend_vin_index, res_output_vout_index) DO UPDATE SET
                spend_block_hash = EXCLUDED.spend_block_hash,
                res_input_txid = EXCLUDED.res_input_txid,
                res_input_vout_index = EXCLUDED.res_input_vout_index,
                res_output_txid = EXCLUDED.res_output_txid,
                res_amount_atoms = EXCLUDED.res_amount_atoms
            `,
            [
              tx.txid,
              firstResInput.vin_index,
              blockHash,
              firstResInput.prev_txid,
              firstResInput.prev_vout,
              tx.txid,
              o.vout_index,
              o.value_atoms,
            ],
          );
        }
      }
    }


    for (const tx of parsedTxs) {
      const outCount = await client.query<{ n: string }>(
        `SELECT COUNT(*)::text AS n FROM outputs WHERE txid = $1`,
        [tx.txid],
      );
      const inCount = await client.query<{ n: string }>(
        `SELECT COUNT(*)::text AS n FROM inputs WHERE txid = $1`,
        [tx.txid],
      );
      if (Number(outCount.rows[0]?.n ?? -1) !== tx.vouts.length || Number(inCount.rows[0]?.n ?? -1) !== tx.vins.length) {
        throw new Error(
          `block ${height} tx ${tx.txid} index incomplete: outputs ${outCount.rows[0]?.n}/${tx.vouts.length} inputs ${inCount.rows[0]?.n}/${tx.vins.length}`,
        );
      }
    }

    await upsertRfc018FeeBurnStats(client, BigInt(height), blockHash, blockRfc018Burn);

    return { blockHash, mismatch };
  });
}

