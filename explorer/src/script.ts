import { bech32, bech32m } from "bech32";

export type AssetKind = "ORU" | "RES" | "NONE" | "INVALID";

export const OP_CONVERGE_OPCODE = 0xbd;
export const OP_CONVERGE_SCRIPT_NAME = "OP_CONVERGE";
export const CONVERGENCE_BURN_SCRIPT_LABEL = "Convergence destroy script";
export const CONVERGENCE_OUTPUT_STATE = "Unspendable destroy output";

/**
 * Historical RFC-014 / retired design: Witness v1 program prefix ASCII "CVG2".
 * Not a live consensus script. Consensus `IsAllowedScript` rejects it.
 */
const RETIRED_CVG2_PREFIX_HEX = Buffer.from("CVG2", "ascii").toString("hex"); // 43564732
const RETIRED_RES2_PREFIX_HEX = Buffer.from("RES2", "ascii").toString("hex"); // 52455332
export const RETIRED_CVG2_V1_SCRIPT_PREFIX_HEX = "5120" + RETIRED_CVG2_PREFIX_HEX;
const OP_RES_MINT = 0xbb;

export type ScriptClassification = {
  assetKind: AssetKind;
  witnessVersion: number | null;
  programHex: string | null;
  address: string | null;
  isConvergence: boolean;
  scriptName: string;
  scriptState: string | null;
};

function normalizeScriptHex(scriptPubKeyHex: string): string {
  return scriptPubKeyHex.trim().toLowerCase().replace(/\s+/g, "");
}

function tryEncodeSegwitAddress(hrp: string, witnessVersion: number, program: Buffer): string | null {
  try {
    const progWords = bech32.toWords(Uint8Array.from(program));
    const words = [witnessVersion, ...progWords];
    if (witnessVersion === 0) {
      return bech32.encode(hrp, words, 90);
    }
    return bech32m.encode(hrp, words, 90);
  } catch {
    return null;
  }
}

/** Live consensus Convergence lock: `OP_PUSHBYTES_20 || 20-byte HASH160 || OP_CONVERGE` (22 bytes). */
export function isOpConvergeScriptPubKey(scriptPubKeyHex: string): boolean {
  const hex = normalizeScriptHex(scriptPubKeyHex);
  if (!/^[0-9a-f]*$/.test(hex) || hex.length !== 44) return false;
  const bytes = Buffer.from(hex, "hex");
  return bytes.length === 22 && bytes[0] === 0x14 && bytes[21] === OP_CONVERGE_OPCODE;
}

/** Historical RFC-014 / retired Witness v1 CVG2 program. Not OP_CONVERGE. */
export function isRetiredCvg2WitnessV1(scriptPubKeyHex: string): boolean {
  return normalizeScriptHex(scriptPubKeyHex).startsWith(RETIRED_CVG2_V1_SCRIPT_PREFIX_HEX);
}

/**
 * True iff the script is a live Convergence burn lock.
 * Mirrors node `IsConvergeScript` / `IsConvergenceScript` for explorer indexing.
 */
export function isConvergenceScriptPubKey(scriptPubKeyHex: string): boolean {
  return isOpConvergeScriptPubKey(scriptPubKeyHex);
}

export function convergenceOutputLabels(): {
  script: string;
  asset: "ORU";
  state: string;
} {
  return {
    script: OP_CONVERGE_SCRIPT_NAME,
    asset: "ORU",
    state: CONVERGENCE_OUTPUT_STATE,
  };
}

/**
 * Height prefix of GetResMintScript: OP_0 or a CScriptNum data push
 * (PUSHBYTES_1 0x05 for height 5, not OP_5). P0-039 / P0-016.
 */
function isCanonicalNonNegIntPush(bytes: Buffer): boolean {
  if (bytes.length === 0) return false;
  if (bytes.length === 1) {
    return bytes[0] === 0x00;
  }
  const plen = bytes[0];
  if (plen < 1 || plen > 5 || bytes.length !== 1 + plen) return false;
  const data = bytes.subarray(1);
  if ((data[data.length - 1] & 0x80) !== 0) return false;
  if (data.length > 1 && data[data.length - 1] === 0 && (data[data.length - 2] & 0x80) === 0) {
    return false;
  }
  return true;
}

/** Canonical OP_RES_MINT: `<height> <20-byte HASH160(pk)> OP_RES_MINT`. */
function isResMintScriptBytes(bytes: Buffer): boolean {
  if (bytes.length < 23 || bytes[bytes.length - 1] !== OP_RES_MINT) return false;
  const pkhOp = bytes.length - 22;
  if (bytes[pkhOp] !== 0x14) return false;
  return isCanonicalNonNegIntPush(bytes.subarray(0, pkhOp));
}

/**
 * True when the bytes match the 0xbb template
 * `<height> <20-byte> 0xbb`.
 * Recognition is not consensus acceptance and is not RES issuance.
 * A body output with this script is `bad-txns-res-mint-noncoinbase`.
 * A RES amount below one atom is `bad-txns-res-output-nonpositive`.
 * A nonzero mainnet coinbase RES amount is `bad-cb-res-amount`.
 */
export function isResMintScriptPubKey(scriptPubKeyHex: string): boolean {
  const hex = normalizeScriptHex(scriptPubKeyHex);
  if (!hex || hex.length % 2 !== 0 || !/^[0-9a-f]*$/.test(hex)) return false;
  return isResMintScriptBytes(Buffer.from(hex, "hex"));
}

/**
 * Script classification used by the indexer.
 * Witness and Convergence rows follow `AssetTypeFromScriptPubKey`.
 * The 0xbb row does not: the C++ classifier names it ASSET_RES, and this
 * indexer refuses to store that name as a confirmed RES output.
 * - OP_CONVERGE (22-byte burn lock) => ORU
 * - v0/20 P2WPKH => ORU
 * - v1/32 native RES (not CVG2, not RES2) => RES
 * - canonical 0xbb template => INVALID (recognized pattern, not a RES output)
 * - OP_RETURN => NONE
 * - anything else, including v0/32, CVG2, RES2, unknown => INVALID
 *
 * C++ `AssetTypeFromScriptPubKey` still names the 0xbb template ASSET_RES.
 * This function does not. Indexing a recognized template as RES would store
 * a consensus-rejected output as confirmed RES. The indexer sees script
 * bytes only; it does not receive a separate acceptance bit, so the
 * conservative kind is INVALID for both coinbase and body outputs.
 */
export function indexedOutputAtoms(assetKind: AssetKind, atoms: bigint): bigint {
  if ((assetKind === "ORU" || assetKind === "RES") && atoms > 0n) return atoms;
  return 0n;
}

/** Confirmed RES balance and supply count only a positive RES-kind output. */
export function countsTowardConfirmedRes(assetKind: AssetKind, atoms: bigint): boolean {
  return assetKind === "RES" && atoms > 0n;
}

export function classifyScriptPubKey(scriptPubKeyHex: string, bech32Hrp: string): ScriptClassification {
  const hex = normalizeScriptHex(scriptPubKeyHex);
  const invalid: ScriptClassification = {
    assetKind: "INVALID",
    witnessVersion: null,
    programHex: null,
    address: null,
    isConvergence: false,
    scriptName: "unknown",
    scriptState: null,
  };
  if (!hex || hex.length % 2 !== 0 || !/^[0-9a-f]*$/.test(hex)) {
    return invalid;
  }
  const bytes = Buffer.from(hex, "hex");

  if (bytes.length >= 1 && bytes[0] === 0x6a) {
    return {
      assetKind: "NONE",
      witnessVersion: null,
      programHex: null,
      address: null,
      isConvergence: false,
      scriptName: "OP_RETURN",
      scriptState: null,
    };
  }

  if (isOpConvergeScriptPubKey(hex)) {
    const program = bytes.subarray(1, 21);
    return {
      assetKind: "ORU",
      witnessVersion: null,
      programHex: Buffer.from(program).toString("hex"),
      address: null,
      isConvergence: true,
      scriptName: OP_CONVERGE_SCRIPT_NAME,
      scriptState: CONVERGENCE_OUTPUT_STATE,
    };
  }

  if (isResMintScriptBytes(bytes)) {
    const pkh = bytes.subarray(bytes.length - 21, bytes.length - 1);
    return {
      assetKind: "INVALID",
      witnessVersion: null,
      programHex: Buffer.from(pkh).toString("hex"),
      address: null,
      isConvergence: false,
      scriptName: "recognized 0xbb template",
      scriptState:
        "Recognized byte pattern only. Not an accepted RES output, and not counted in confirmed RES balance or supply.",
    };
  }

  // Witness v0: OP_0 <20> only. v0/32 P2WSH is ASSET_INVALID.
  if (bytes.length === 22 && bytes[0] === 0x00 && bytes[1] === 0x14) {
    const program = bytes.subarray(2, 22);
    const programHex = Buffer.from(program).toString("hex");
    const address = tryEncodeSegwitAddress(bech32Hrp, 0, Buffer.from(program));
    return {
      assetKind: "ORU",
      witnessVersion: 0,
      programHex,
      address,
      isConvergence: false,
      scriptName: "Witness v0",
      scriptState: null,
    };
  }
  if (bytes.length === 34 && bytes[0] === 0x00 && bytes[1] === 0x20) {
    const program = bytes.subarray(2, 34);
    const programHex = Buffer.from(program).toString("hex");
    const address = tryEncodeSegwitAddress(bech32Hrp, 0, Buffer.from(program));
    return {
      assetKind: "INVALID",
      witnessVersion: 0,
      programHex,
      address,
      isConvergence: false,
      scriptName: "Witness v0/32 (not a consensus output)",
      scriptState: null,
    };
  }

  // Witness v1: OP_1 <32> — native RES only. Retired CVG2/RES2 prefixes are INVALID.
  if (bytes.length === 34 && bytes[0] === 0x51 && bytes[1] === 0x20) {
    const program = bytes.subarray(2, 34);
    const programHex = Buffer.from(program).toString("hex");
    const address = tryEncodeSegwitAddress(bech32Hrp, 1, Buffer.from(program));
    if (programHex.startsWith(RETIRED_CVG2_PREFIX_HEX)) {
      return {
        assetKind: "INVALID",
        witnessVersion: 1,
        programHex,
        address,
        isConvergence: false,
        scriptName: "retired Witness v1 CVG2",
        scriptState: "Historical RFC-014 / retired design",
      };
    }
    if (programHex.startsWith(RETIRED_RES2_PREFIX_HEX)) {
      return {
        assetKind: "INVALID",
        witnessVersion: 1,
        programHex,
        address,
        isConvergence: false,
        scriptName: "retired Witness v1 RES2",
        scriptState: "Retired RES2 prefix; not creatable",
      };
    }
    return {
      assetKind: "RES",
      witnessVersion: 1,
      programHex,
      address,
      isConvergence: false,
      scriptName: "Witness v1 RES",
      scriptState: null,
    };
  }

  return invalid;
}
