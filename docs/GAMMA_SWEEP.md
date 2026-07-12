# Gamma sweep — Avellaneda-Stoikov on BTC/USDT

Reproduce: `scripts/gamma_sweep.sh` (replays the committed 30-min fixture through
the strategy for each γ; deterministic, offline). Fixed: κ=1.5, τ=1 day, σ EWMA
halflife 60s, q_max=100 lots, quote_size=10 lots.

## Results (30-min BTCUSDT fixture, 17,999 events)

| gamma | quotes | fills | final_q | max\|q\| | mean_q | cross_viol |
|---|---|---|---|---|---|---|
| 1.0 | 17999 | 0 | 0 | 0 | 0.00 | 0 |
| 0.1 | 17999 | 0 | 0 | 0 | 0.00 | 0 |
| 0.01 | 17999 | 0 | 0 | 0 | 0.00 | 0 |
| 1e-4 | 17999 | 0 | 0 | 0 | 0.00 | 0 |
| 1e-6 | 17999 | 12 | 100 | 100 | 55.96 | 0 |
| 1e-8 | 17999 | 185 | 50 | 100 | 51.76 | 0 |
| 1e-9 | 17999 | 191 | 50 | 100 | 50.22 | 0 |
| 1e-10 | 17999 | 193 | 50 | 100 | 47.80 | 0 |

## The finding (this is the honest, interview-worthy result)

**The prescribed grid {0.01, 0.1, 1.0} produces ZERO fills.** At BTC's price scale
the absolute price volatility in ticks is enormous — σ_p ≈ 36,000 ticks/√day
(measured live). The A-S spread's inventory term is `γ·σ_p²·τ`; with σ_p² ≈ 1.3e9,
even γ=0.01 quotes a spread of ~13 million ticks (~$130k). Nothing ever fills.

**Fills appear only for γ ≤ 1e-6**, i.e. 4–10 orders of magnitude below the
textbook grid. But there the inventory-control term `q·γ·σ_p²·τ` that should shade
the reservation price to mean-revert inventory is *also* tiny, so the reservation
price barely leans against inventory. Result: symmetric quoting bounded only by the
hard inventory limit — mean_q sits near half of q_max and pins at ±q_max, rather
than mean-reverting to 0. **There is no γ in this unit convention that both fills
AND controls inventory the classic A-S way** — the two regimes don't overlap.

`cross_viol = 0` at every γ confirms the post-only / never-cross invariant holds.

## Why, and what it implies

The A-S model was derived for a single mid-priced asset where γ, σ, and price are
O(1)-comparable. Applied verbatim to BTC in raw tick units, γ must absorb the
σ_p² ≈ 1e9 scale, collapsing the useful range to ~1e-9. This is a units/scaling
artifact, not a coding bug (the equations are implemented exactly — verified
against MODEL.md). Candidate fixes (future work, not in Phase 4 scope): normalize σ
to relative terms so γ stays O(0.01–1); or decouple the spread from the
inventory-skew by parameterizing them separately (a common practical A-S variant).

The live 48h paper run uses **γ=1e-9** (the config default): it fills at a
reasonable rate (~190 fills / 30 min extrapolated) with inventory bounded by the
hard limits. The run's inventory time series (via `analysis/paper_report.py`) will
show whether it mean-reverts over multiple regimes or drifts — the honest test the
30-min fixture is too short to settle.
