# RIGOR Protocol Whitepaper folder

**Start here:** [`RIGOR_PROTOCOL_WHITEPAPER.md`](./RIGOR_PROTOCOL_WHITEPAPER.md)

That file is the public protocol narrative. It is not protocol SSOT;
live RFCs are.

```
equivalence_claimed = false
```

Short companions (audit-sized answers, not a substitute for the paper):

| Document | Question |
|----------|----------|
| [`RIGOR_PROTOCOL_IDENTITY_ONE_PAGE.md`](./RIGOR_PROTOCOL_IDENTITY_ONE_PAGE.md) | One-page identity |
| [`RIGOR_PROTOCOL_IDENTITY.md`](./RIGOR_PROTOCOL_IDENTITY.md) | What is RIGOR? |
| [`RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md`](./RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md) | How does the mathematics run? |
| [`RIGOR_FORMAL_VERIFICATION_DESIGN.md`](./RIGOR_FORMAL_VERIFICATION_DESIGN.md) | What is the model, and where does proof stop? (live claims: chapter 8 / SOT) |

Chapters bound into the paper:

| Document | Role |
|----------|------|
| [`chapters/01-single-asset-problem.md`](./chapters/01-single-asset-problem.md) | Chapter 1 — why RIGOR separates two monetary functions |
| [`chapters/02-two-assets.md`](./chapters/02-two-assets.md) | Chapter 2 — rules and parameters |
| [`chapters/03-native-utxo.md`](./chapters/03-native-utxo.md) | Chapter 3 — native UTXO identities |
| [`chapters/04-convergence.md`](./chapters/04-convergence.md) | Chapter 4 — Convergence as the one-way bridge |
| [`chapters/05-mathematical-fairness.md`](./chapters/05-mathematical-fairness.md) | Chapter 5 — checkable mathematics |
| [`chapters/06-mining.md`](./chapters/06-mining.md) | Chapter 6 — proof-of-work, shared height |
| [`chapters/07-self-proving.md`](./chapters/07-self-proving.md) | Chapter 7 — local verification |
| [`chapters/08-formal-boundary.md`](./chapters/08-formal-boundary.md) | Chapter 8 — formal boundary |
| [`chapters/09-product-surfaces.md`](./chapters/09-product-surfaces.md) | Chapter 9 — product surfaces |
| [`chapters/10-security-model.md`](./chapters/10-security-model.md) | Chapter 10 — security model and threat boundaries |
| [`chapters/11-trade-offs.md`](./chapters/11-trade-offs.md) | Chapter 11 — design trade-offs |
| [`chapters/12-evolution-policy.md`](./chapters/12-evolution-policy.md) | Chapter 12 — deployment and evolution policy |
| [`chapters/10-appendix.md`](./chapters/10-appendix.md) | Appendix — protocol constants and engineering reference |
| [`RIGOR_CONVERGENCE_MATHEMATICAL_NOTE.md`](./RIGOR_CONVERGENCE_MATHEMATICAL_NOTE.md) | Appendix D — Convergence mathematical design note |
| [`chapters/09-glossary.md`](./chapters/09-glossary.md) | Noun list (not Chapter 9 narrative) |
| [`RIGOR_TECHNICAL_WHITEPAPER_STRUCTURE.md`](./RIGOR_TECHNICAL_WHITEPAPER_STRUCTURE.md) | Technical paper structure |
| [`chapters/RIGOR_MINIMAL_SCRIPT_ARCHITECTURE.md`](./chapters/RIGOR_MINIMAL_SCRIPT_ARCHITECTURE.md) | Minimal script chapter |

Implementation facts: [`../developer/CODE_SSOT_PROTOCOL_FACTS.md`](../developer/CODE_SSOT_PROTOCOL_FACTS.md).
Opcode spec: [`../specification/RIGOR_OPCODE_SPECIFICATION.md`](../specification/RIGOR_OPCODE_SPECIFICATION.md).

RFC-019 is not the production issuance rule.
Live PoW name is **OpuHash** = FIPS SHA3-256(`"RGRPOW01" || header_80`). `CheckProofOfWork` calls `GetRigorPowHash`. It does not call `GetOpusHash`.
