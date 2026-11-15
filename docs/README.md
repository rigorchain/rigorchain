# RIGOR documentation

Protocol specifications are normative SSOT.
Implementations realize the frozen protocol.

```
equivalence_claimed = false
documentation_consensus_authority = false
refinement_proved = false
LEVEL_5_CLAIMED = false
CPP_VERIFIED = false
ARCHIVE_NOT_NORMATIVE = true
```

Repository entry: [`../README.md`](../README.md).

## Active tree

```
docs/
├── README.md                 ← this file
├── protocol/                 ← live protocol
├── developer/                ← build / architecture / RPC / tests
├── user/                     ← wallet / mining
├── formal/                   ← public verification claims
├── specification/            ← SPEC-000…008 (describe current code)
└── whitepaper/               ← public papers
```

| Tree | Audience |
|------|----------|
| [`protocol/`](./protocol/) | RFC-020, RFC-018, consensus invariants, assets index |
| [`developer/`](./developer/) | Architecture, build, RPC, testing |
| [`user/`](./user/) | Wallet and mining |
| [`formal/`](./formal/) | Verification status and claims |
| [`specification/`](./specification/) | Current SPEC layer |
| [`whitepaper/`](./whitepaper/) | Public protocol papers |

Protocol authority is the live RFC and specification set linked below.
There is no `archive/` tree in this repository.

## Start here

| Document | Purpose |
|----------|---------|
| [`whitepaper/RIGOR_PROTOCOL_WHITEPAPER.md`](./whitepaper/RIGOR_PROTOCOL_WHITEPAPER.md) | Public protocol narrative |
| [`protocol/RFC-020.md`](./protocol/RFC-020.md) | Deterministic Economics for Human and Machine Verification. Live issuance rule: continuous RES destroy cost, flat ORU emission |
| [`protocol/RFC-018.md`](./protocol/RFC-018.md) | RES-transfer fee burn |
| [`protocol/RIGOR_CONSENSUS_INVARIANTS.md`](./protocol/RIGOR_CONSENSUS_INVARIANTS.md) | Mainnet consensus boundary |
| [`protocol/Consensus.md`](./protocol/Consensus.md) | Consensus index |
| [`protocol/Assets.md`](./protocol/Assets.md) | Asset index |
| [`developer/Build.md`](./developer/Build.md) | Compile and run |
| [`user/Wallet.md`](./user/Wallet.md) | Wallet workflows |
| [`user/Mining.md`](./user/Mining.md) | SOLO mining |
| [`formal/VerificationStatus.md`](./formal/VerificationStatus.md) | Formal flags |
| [`whitepaper/RIGOR_PROTOCOL_IDENTITY.md`](./whitepaper/RIGOR_PROTOCOL_IDENTITY.md) | Short identity companion |

## Governance

```
DOCUMENTATION_STATUS=FROZEN
ACTIVE_DOCS_ONLY=true
ARCHIVE_NOT_NORMATIVE=true
HISTORICAL_DOCS_NOT_PROTOCOL=true
```

Do not add RFC-change commentary as a new active paper. Update the live
RFC file or archive the previous one. Process reports belong in
`archive/`, not here.
