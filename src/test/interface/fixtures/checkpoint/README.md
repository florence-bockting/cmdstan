# Checkpoint test fixtures

Generated after checkpoint support is implemented. Run:

```bash
./src/test/interface/fixtures/checkpoint/regenerate_fixtures.sh
```

Files:
- `golden_full.csv` — uninterrupted `eight_schools` diag_e run (seed=1234, 800 sampling draws)
- `checkpoint_at_500.json` — checkpoint at absolute iteration 500 for the diag_e test
- `dense/golden_full.csv` — uninterrupted `test_model` dense_e run (seed=5678)
- `dense/checkpoint_at_500.json` — dense_e checkpoint at iteration 500
- `multi_chain/golden_chain_{1,2}.csv` — two-chain `eight_schools` run (seed=9012)
- `multi_chain/checkpoint_chain_{1,2}_at_500.json` — per-chain checkpoints at iteration 500

Checkpoints store the full post-transition `mcmc::sample`: `last_position`, `last_lp`,
and `last_accept_stat`.
