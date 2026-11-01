import { describe, it } from "node:test";
import assert from "node:assert/strict";
import {
  ORU_COIN,
  RES_COIN,
  cumulativeBurnFromBlockBurns,
  destroyedFeeOruForBlock,
  nextCumulativeBurn,
  oruResTransferFeeBurnShare,
  parentPointerCumulative,
  rfc018DestroyedFeeOru,
  rollbackCumulativeBurn,
  rfc018FeeBurnView,
} from "./rfc018FeeBurn";

const oru = (n: bigint): { asset_kind: "ORU"; value_atoms: bigint } => ({
  asset_kind: "ORU",
  value_atoms: n,
});
const res = (n: bigint): { asset_kind: "RES"; value_atoms: bigint } => ({
  asset_kind: "RES",
  value_atoms: n,
});

describe("RFC-018 explorer fee-burn statistics", () => {
  it("locks 1/2 floor share and eight-decimal coins (not 1e6 or 100 atoms)", () => {
    assert.equal(ORU_COIN, 100000000n);
    assert.equal(RES_COIN, 100000000n);
    assert.notEqual(RES_COIN, 100n);
    assert.notEqual(RES_COIN, 1000000n);
    assert.equal(oruResTransferFeeBurnShare(0n), 0n);
    assert.equal(oruResTransferFeeBurnShare(1n), 0n);
    assert.equal(oruResTransferFeeBurnShare(2n), 1n);
    assert.equal(oruResTransferFeeBurnShare(1000n), 500n);
    assert.equal(oruResTransferFeeBurnShare(ORU_COIN), ORU_COIN / 2n);
  });

  it("burns floor(ORU_fee/2) on a RES-transfer block", () => {
    const burn = rfc018DestroyedFeeOru({
      isCoinbase: false,
      isResMint: false,
      inputs: [res(40n * RES_COIN), oru(ORU_COIN)],
      outputs: [res(40n * RES_COIN), oru(ORU_COIN - 1000n)],
    });
    assert.equal(burn, 500n);
  });

  it("burns nothing on a block without RES-transfer fees", () => {
    const pureOru = rfc018DestroyedFeeOru({
      isCoinbase: false,
      isResMint: false,
      inputs: [oru(ORU_COIN)],
      outputs: [oru(ORU_COIN - 2000n)],
    });
    const coinbase = rfc018DestroyedFeeOru({
      isCoinbase: true,
      isResMint: false,
      inputs: [],
      outputs: [oru(125n * ORU_COIN)],
    });
    const mint = rfc018DestroyedFeeOru({
      isCoinbase: false,
      isResMint: true,
      inputs: [oru(1024n * ORU_COIN)],
      outputs: [res(40n * RES_COIN), oru(ORU_COIN)],
    });
    assert.equal(pureOru, 0n);
    assert.equal(coinbase, 0n);
    assert.equal(mint, 0n);
  });

  it("sums multiple RES-transfer transactions in one block", () => {
    const txs = [
      {
        isCoinbase: true,
        isResMint: false,
        inputs: [] as Array<{ asset_kind: "ORU" | "RES"; value_atoms: bigint }>,
        outputs: [oru(125n * ORU_COIN)],
      },
      {
        isCoinbase: false,
        isResMint: false,
        inputs: [res(RES_COIN), oru(2n * ORU_COIN)],
        outputs: [res(RES_COIN), oru(2n * ORU_COIN - 4000n)],
      },
      {
        isCoinbase: false,
        isResMint: false,
        inputs: [res(RES_COIN), oru(ORU_COIN)],
        outputs: [res(RES_COIN), oru(ORU_COIN - 1000n)],
      },
    ];
    assert.equal(destroyedFeeOruForBlock(txs), 2000n + 500n);
  });

  it("accumulates cumulative burn across blocks", () => {
    const b0 = 0n;
    const b1 = 500n;
    const b2 = 0n;
    const b3 = 1500n;
    const c1 = nextCumulativeBurn(0n, b0);
    const c2 = nextCumulativeBurn(c1, b1);
    const c3 = nextCumulativeBurn(c2, b2);
    const c4 = nextCumulativeBurn(c3, b3);
    assert.equal(c1, 0n);
    assert.equal(c2, 500n);
    assert.equal(c3, 500n);
    assert.equal(c4, 2000n);
  });

  it("rolls back the disconnected tip without summing the abandoned branch", () => {
    const parentCum = 500n;
    const oldTipBurn = 1500n;
    const newBranchBurn = 100n;
    const afterOldTip = nextCumulativeBurn(parentCum, oldTipBurn);
    assert.equal(afterOldTip, 2000n);
    const restored = rollbackCumulativeBurn(afterOldTip, oldTipBurn);
    assert.equal(restored, parentCum);
    const afterNewTip = nextCumulativeBurn(restored, newBranchBurn);
    assert.equal(afterNewTip, 600n);
    assert.notEqual(afterNewTip, parentCum + oldTipBurn + newBranchBurn);
  });

  it("burns floor(fee/2) for an odd RES-transfer fee and nothing for a one-atom fee", () => {
    const odd = rfc018DestroyedFeeOru({
      isCoinbase: false,
      isResMint: false,
      inputs: [res(97653880n), oru(58591052n)],
      outputs: [res(10000000n), res(87653880n), oru(58590643n)],
    });
    const oneAtom = rfc018DestroyedFeeOru({
      isCoinbase: false,
      isResMint: false,
      inputs: [res(RES_COIN), oru(100n)],
      outputs: [res(RES_COIN), oru(99n)],
    });
    assert.equal(58591052n - 58590643n, 409n);
    assert.equal(odd, 204n);
    assert.equal(oneAtom, 0n);
  });

  it("does not classify a pure ORU spend or a RES mint as RFC-018 destruction", () => {
    const oruSpend = rfc018DestroyedFeeOru({
      isCoinbase: false,
      isResMint: false,
      inputs: [oru(128n * ORU_COIN)],
      outputs: [oru(128n * ORU_COIN - 214n)],
    });
    const mint = rfc018DestroyedFeeOru({
      isCoinbase: false,
      isResMint: true,
      inputs: [oru(128n * ORU_COIN), oru(128n * ORU_COIN), oru(128n * ORU_COIN)],
      outputs: [oru(380n * ORU_COIN), oru(399999463n), res(37108631n)],
    });
    const mintWithoutFlag = rfc018DestroyedFeeOru({
      isCoinbase: false,
      isResMint: false,
      inputs: [oru(128n * ORU_COIN), oru(128n * ORU_COIN), oru(128n * ORU_COIN)],
      outputs: [oru(380n * ORU_COIN), oru(399999463n), res(37108631n)],
    });
    assert.equal(384n * ORU_COIN - (380n * ORU_COIN + 399999463n), 537n);
    assert.equal(oruSpend, 0n);
    assert.equal(mint, 0n);
    assert.equal(mintWithoutFlag, 0n);
  });

  it("keeps an OP_CONVERGE output out of the RES-transfer fee burn", () => {
    const transfer = rfc018DestroyedFeeOru({
      isCoinbase: false,
      isResMint: false,
      inputs: [res(49803372n), oru(89998852n)],
      outputs: [res(49803372n), oru(89998346n)],
    });
    assert.equal(89998852n - 89998346n, 506n);
    assert.equal(transfer, 253n);
    assert.notEqual(transfer, 253n + 380n * ORU_COIN);
  });

  it("sums distinct qualifying transfers and ignores a replay of the same height", () => {
    const rows = [
      { height: 845, burned: 204n },
      { height: 1185, burned: 253n },
      { height: 1185, burned: 253n },
      { height: 1386, burned: 0n },
    ];
    assert.equal(cumulativeBurnFromBlockBurns(rows), 457n);
    assert.equal(
      cumulativeBurnFromBlockBurns([
        { height: 845, burned: 204n },
        { height: 1185, burned: null },
      ]),
      null,
    );
  });

  it("does not let a missing parent height restart the canonical sum at zero", () => {
    const rows: Array<{ height: number; burned: bigint }> = [];
    for (let height = 1; height <= 1386; height += 1) {
      const burned = height === 845 ? 204n : height === 1185 ? 253n : 0n;
      rows.push({ height, burned });
    }
    rows.push({ height: 1388, burned: 0n });
    const pointer = parentPointerCumulative(rows);
    assert.equal(pointer.get(1386), 457n);
    assert.equal(pointer.get(1388), 0n);
    assert.equal(cumulativeBurnFromBlockBurns(rows), 457n);
    const afterReorg = rows.filter((row) => row.height < 1388);
    assert.equal(cumulativeBurnFromBlockBurns(afterReorg), 457n);
    assert.notEqual(cumulativeBurnFromBlockBurns(afterReorg), pointer.get(1388));
  });

  it("keeps fee-burn view fields separate from circulating and Convergence", () => {
    const view = rfc018FeeBurnView(500n, 1500n, (a) => `${a} atoms`);
    assert.equal(view.block_burned_oru, "500");
    assert.equal(view.cumulative_burned_oru, "1500");
    assert.equal(view.equivalence_claimed, false);
    assert.equal("oru_circulating_spendable" in view, false);
    assert.equal("cumulative_convergence" in view, false);
    assert.equal("total_issued_oru" in view, false);
  });
});
