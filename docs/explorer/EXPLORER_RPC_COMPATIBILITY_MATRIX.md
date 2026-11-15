# Explorer RPC compatibility

This page records two different facts, both read from source. No node was started, and no RPC was executed.

1. The method is registered in the node source.
2. `explorer/src/rpcCli.ts` actually calls it.

**STABLE** below means only the second fact: this explorer's CLI wrapper calls the method. A registered method that this explorer does not call is not marked STABLE. Parameter and result names below are the `RPCHelpMan` declarations. Runtime JSON was not captured.

Registration lists read for this page:

- `core/src/rpc/blockchain.cpp` `RegisterBlockchainRPCCommands`
- `core/src/rpc/mempool.cpp` `RegisterMempoolRPCCommands`
- `core/src/rpc/rawtransaction.cpp` registration array
- `core/src/rpc/mining.cpp` `submitblock`
- `core/src/wallet/rpc/rpcwallet.cpp` wallet registration
- `core/src/rpc/net.cpp` `getnetworkinfo`

## Methods this explorer calls

| Method | Registration | What the source declares | Explorer call |
|--------|--------------|--------------------------|---------------|
| `getblockhash` | `blockchain.cpp` | height → block hash | `rpcCli.ts`, importer, startup |
| `getblock` | `blockchain.cpp` | verbosity argument; result includes `cumulative_convergence` and `rigor_res_minted` | importer uses verbosity 1 and 2 |
| `getrawtransaction` | `rawtransaction.cpp` | txid, optional verbose flag | importer calls verbose `true` |
| `getblockchaininfo` | `blockchain.cpp` | chain summary; result includes `cumulative_convergence` | status, importer, compliance |
| `getconvergencestatus` | `blockchain.cpp` | no arguments. Fields: `tip_height`, `confirm_height`, `issuance_authorized`, and, when authorized, `destroy_cost`, `min_atom_burn`, `min_whole_res_burn`, `destroy_cost_next`, `next_height`. Also `res_total`, `cumulative_convergence`, `last_res_height` | status and economics, with a catch that substitutes `{}` |
| `getruntimesupply` | `blockchain.cpp` | tip supply witness | status and economics, same catch |
| `getnetworkinfo` | `net.cpp` | local network summary | status and compliance |
| `getrawmempool` | `mempool.cpp` | `verbose` defaults to false and then returns a transaction-id array | called with no arguments, so the default applies. This call does not request the verbose `convergence` object |

`rigor_res_minted` is `CBlockIndex::has_res`. ConnectBlock sets it to 1 when the block's body RES mint amount is positive, and to 0 otherwise. The help text's phrase "convergence cycle" is the help string, not a second rule.

`destroy_cost` is `ResDestroyCostAtHeight`. It is a display value. The mint rule remains the RFC-020 inequality.

## Registered, not called by this explorer

These names are in a `CRPCCommand` table. This explorer's `RpcMethod` union does not include them. Do not describe them as explorer-supported.

| Method | Registered in | Declared shape |
|--------|---------------|----------------|
| `getblockheader` | `blockchain.cpp` | block header |
| `getbestblockhash` | `blockchain.cpp` | tip hash |
| `getblockcount` | `blockchain.cpp` | height |
| `getdifficulty` | `blockchain.cpp` | difficulty |
| `gettxout` | `blockchain.cpp` | one UTXO |
| `gettxoutsetinfo` | `blockchain.cpp` | UTXO statistics |
| `getblockstats` | `blockchain.cpp` | block statistics |
| `scantxoutset` | `blockchain.cpp` | descriptor scan |
| `getblockconvergence` | `blockchain.cpp` | `hash`, `height`, `cumulative_convergence` |
| `getassetbalance` | `blockchain.cpp` | `address`, `asset` (`ORU` or `RES`); scans the UTXO cursor |
| `getdeterministicstatehash` | `blockchain.cpp` | `state_hash`, `height`, `best_block_hash`. The help text says the call walks the UTXO set under `cs_main` |
| `getreplaysnapshot` | `blockchain.cpp` | snapshot object |
| `getchainstatesummary` | `blockchain.cpp` | summary object |
| `diffchainstatesummaries` | `blockchain.cpp` | comparison |
| `listconvergencehistory` | `rawtransaction.cpp` | `start_height`, `end_height` |
| `decoderawtransaction` | `rawtransaction.cpp` | hex in, decoded transaction out |
| `decodescript` | `rawtransaction.cpp` | hex script in |
| `getmempoolinfo` | `mempool.cpp` | includes optional `cumulative_convergence` |
| `getmempoolentry` | `mempool.cpp` | one entry, including a `convergence` object |
| `getdeploymentinfo` | `blockchain.cpp` | deployment object. This explorer does not call it |

Verbose `getrawmempool`, `getmempoolentry`, and the `convergence` object exist in `mempool.cpp`. Fields declared on that object are `is_convergence`, `convergence_amount`, `min_convergence`, `meets_min_convergence`, `descendant_feerate`, `eviction_score`, `policy`, and `priority`. This explorer does not request that object.

Wallet methods `createconvergence`, `sendres`, and `getbalances` are registered in `core/src/wallet/rpc/rpcwallet.cpp`. They are present when the wallet RPC table is registered. This explorer does not call them.

`createrawtransaction` and `signrawtransactionwithkey` are registered raw-transaction methods. `submitblock`, `preciousblock`, `invalidateblock`, and `reconsiderblock` are registered and change node state. This explorer does not call them.

## Names that are not registered

`getconvergenceresult`, `getconvergencedrawcandidates`, `getresmintstatus`, `getconvergeinfo`, and `createresclaim` do not appear in the registration arrays above. `feature_rigor_convergence_rpc_status_history.py` asserts that `getconvergeinfo` is absent from `help`.

`rigor_target`, `res_draw_target`, and `res_target_ctx` are not fields of `getblockconvergence` or `getconvergencestatus`.
