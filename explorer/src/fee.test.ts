import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { computeTxFee, formatOruAtoms } from "./fee";
import { oruLegFee, oruResTransferFeeBurnShare } from "./rfc018FeeBurn";

describe("explorer fee display", () => {
  it("coinbase fee is 0", () => {
    const f = computeTxFee({
      isCoinbase: true,
      oruInputAtoms: null,
      oruOutputAtoms: 1000n * 100000000n,
      involvesRes: false,
    });
    assert.equal(f.fee_kind, "coinbase");
    assert.equal(f.fee_display, "0");
    assert.equal(f.fee_atoms, "0");
  });

  it("ORU fee is inputs minus outputs", () => {
    const f = computeTxFee({
      isCoinbase: false,
      oruInputAtoms: 100000000n,
      oruOutputAtoms: 99999000n,
      involvesRes: false,
    });
    assert.equal(f.fee_kind, "oru");
    assert.equal(f.fee_atoms, "1000");
    assert.equal(f.fee_display, formatOruAtoms(1000n));
  });

  it("shows the full ORU leg when the transaction also moves RES", () => {
    const inputs = [
      { asset_kind: "ORU" as const, value_atoms: 89998852n },
      { asset_kind: "RES" as const, value_atoms: 49803372n },
    ];
    const outputs = [
      { asset_kind: "ORU" as const, value_atoms: 89998346n },
      { asset_kind: "RES" as const, value_atoms: 49803372n },
    ];
    const leg = oruLegFee(inputs, outputs);
    const burn = oruResTransferFeeBurnShare(leg);
    const f = computeTxFee({
      isCoinbase: false,
      oruInputAtoms: 89998852n,
      oruOutputAtoms: 89998346n,
      involvesRes: true,
    });
    assert.equal(leg, 506n);
    assert.equal(burn, 253n);
    assert.equal(f.fee_kind, "oru");
    assert.equal(f.fee_atoms, "506");
    assert.equal(f.fee_display, formatOruAtoms(506n));
    assert.notEqual(f.fee_atoms, burn.toString());
    assert.notEqual(f.fee_atoms, "49803372");
  });

  it("does not display a mint's destroyed ORU or minted RES as the fee", () => {
    const f = computeTxFee({
      isCoinbase: false,
      oruInputAtoms: 38400000000n,
      oruOutputAtoms: 38000000000n + 399999463n,
      involvesRes: true,
    });
    assert.equal(f.fee_atoms, "537");
    assert.equal(f.fee_display, formatOruAtoms(537n));
    assert.notEqual(f.fee_atoms, "37108631");
    assert.notEqual(f.fee_atoms, "38000000000");
  });

  it("keeps the full 409-atom ORU leg instead of the 204-atom RFC-018 share", () => {
    const leg = oruLegFee(
      [
        { asset_kind: "ORU", value_atoms: 100000000n },
        { asset_kind: "RES", value_atoms: 97653880n },
      ],
      [
        { asset_kind: "ORU", value_atoms: 100000000n - 409n },
        { asset_kind: "RES", value_atoms: 10000000n },
        { asset_kind: "RES", value_atoms: 87653880n },
      ],
    );
    const f = computeTxFee({
      isCoinbase: false,
      oruInputAtoms: 100000000n,
      oruOutputAtoms: 100000000n - 409n,
      involvesRes: true,
    });
    assert.equal(leg, 409n);
    assert.equal(oruResTransferFeeBurnShare(leg), 204n);
    assert.equal(f.fee_atoms, "409");
    assert.notEqual(f.fee_atoms, "204");
  });

  it("missing inputs is N/A", () => {
    const f = computeTxFee({
      isCoinbase: false,
      oruInputAtoms: null,
      oruOutputAtoms: 1n,
      involvesRes: false,
    });
    assert.equal(f.fee_display, "N/A");
  });
});
