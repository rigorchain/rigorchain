/**
 * RES transfer presentation. A transfer spends RES and creates RES outputs.
 * A mint creates RES without a RES input. Destruction of ORU is not a RES transfer.
 * Amounts stay in integer atoms until display.
 */

export type SenderState =
  | { kind: "address"; address: string }
  | { kind: "multiple" }
  | { kind: "unavailable" };

export type ResTransferInput = {
  address: string | null;
  atoms: bigint;
};

export type ResTransferOutput = {
  vout: number;
  address: string | null;
  atoms: bigint;
};

export type ResTransferTx = {
  height: number;
  blockHash: string;
  txIndex: number;
  txid: string;
  time: number | null;
  canonical: boolean;
  isCoinbase: boolean;
  isResMint: boolean;
  inputs: ResTransferInput[];
  outputs: ResTransferOutput[];
};

export type ResTransferRow = {
  height: number;
  blockHash: string;
  txIndex: number;
  txid: string;
  time: number | null;
  vout: number;
  amountAtoms: bigint;
  sender: SenderState;
  recipient: string | null;
};

export function isOrdinaryResTransfer(tx: Pick<ResTransferTx, "canonical" | "isCoinbase" | "isResMint" | "inputs" | "outputs">): boolean {
  if (!tx.canonical || tx.isCoinbase || tx.isResMint) return false;
  const resIn = tx.inputs.reduce((sum, input) => sum + (input.atoms > 0n ? input.atoms : 0n), 0n);
  const resOut = tx.outputs.reduce((sum, output) => sum + (output.atoms > 0n ? output.atoms : 0n), 0n);
  return resIn > 0n && resOut > 0n;
}

export function senderAttribution(inputs: ResTransferInput[]): SenderState {
  const spent = inputs.filter((input) => input.atoms > 0n);
  const known = [...new Set(spent.map((input) => input.address).filter((address): address is string => Boolean(address)))];
  const missing = spent.some((input) => !input.address);
  if (known.length > 1 || (known.length === 1 && missing)) return { kind: "multiple" };
  if (known.length === 1) return { kind: "address", address: known[0] };
  return { kind: "unavailable" };
}

export function transferRowsForTx(tx: ResTransferTx): ResTransferRow[] {
  if (!isOrdinaryResTransfer(tx)) return [];
  const sender = senderAttribution(tx.inputs);
  return tx.outputs
    .filter((output) => output.atoms > 0n)
    .sort((a, b) => a.vout - b.vout)
    .map((output) => ({
      height: tx.height,
      blockHash: tx.blockHash,
      txIndex: tx.txIndex,
      txid: tx.txid,
      time: tx.time,
      vout: output.vout,
      amountAtoms: output.atoms,
      sender,
      recipient: output.address,
    }));
}

export function compareTransferRows(a: ResTransferRow, b: ResTransferRow): number {
  if (a.height !== b.height) return b.height - a.height;
  if (a.txIndex !== b.txIndex) return b.txIndex - a.txIndex;
  if (a.vout !== b.vout) return a.vout - b.vout;
  return a.txid < b.txid ? -1 : a.txid > b.txid ? 1 : 0;
}

export function canonicalTransferRows(txs: ResTransferTx[]): ResTransferRow[] {
  const rows = txs.flatMap(transferRowsForTx);
  rows.sort(compareTransferRows);
  return rows;
}

export function pageTransferRows(rows: ResTransferRow[], page: number, limit: number): ResTransferRow[] {
  const start = Math.max(0, (page - 1) * limit);
  return rows.slice(start, start + limit);
}

export function addressResCounterparty(args: {
  sentAtoms: bigint;
  receivedAtoms: bigint;
  senderAddressCount: number;
  senderAddress: string | null;
  senderMissing: boolean;
  otherRecipientCount: number;
  otherRecipient: string | null;
}): SenderState {
  if (args.sentAtoms > 0n) {
    if (args.otherRecipientCount > 1) return { kind: "multiple" };
    if (args.otherRecipientCount === 1 && args.otherRecipient) return { kind: "address", address: args.otherRecipient };
    return { kind: "unavailable" };
  }
  if (args.receivedAtoms > 0n) {
    if (args.senderAddressCount > 1 || (args.senderAddressCount === 1 && args.senderMissing)) return { kind: "multiple" };
    if (args.senderAddressCount === 1 && args.senderAddress) return { kind: "address", address: args.senderAddress };
  }
  return { kind: "unavailable" };
}
