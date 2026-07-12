# MODEL — Avellaneda-Stoikov quoting

Reference: Avellaneda & Stoikov, "High-frequency trading in a limit order book",
Quantitative Finance 8(3), 2008. Read the paper once; implement the equations
below (the standard infinite-horizon / rolling-horizon practical variant).

## Intuition (the version you say in interviews)
A market maker earns the spread but accumulates inventory, and inventory is risk:
if you're long 5 BTC and the price drops, spread income is wiped out. A-S solves
the stochastic control problem and the answer has two clean parts:
1. Quote around a **reservation price** that leans away from your inventory
   (long ⇒ shade quotes down to attract buyers of your inventory).
2. Set the **spread** from volatility, risk aversion, and how quickly fill
   probability decays as you quote further from mid (κ).

## Equations implemented
Let s = mid price, q = signed inventory (base units), σ = volatility of s
(per √time-unit), γ = risk aversion, κ = order-book liquidity/decay parameter,
T−t = time horizon (we use a rolling constant τ, e.g. 1.0 in day units —
document the choice in DECISIONS.md).

Reservation price:
    r = s − q · γ · σ² · τ

Optimal total spread:
    δ_total = γ · σ² · τ + (2/γ) · ln(1 + γ/κ)

Quotes (before rounding/clamping):
    bid = r − δ_total/2
    ask = r + δ_total/2

## Parameter estimation
- **σ**: EWMA std of 1s mid log-returns, annualization-free (keep units
  consistent with τ; write the unit convention here once chosen — unit bugs
  are the #1 way this model silently misbehaves).

  **Unit convention (chosen in Phase 4):** time unit = 1 day, so `τ = 1.0`.
  `SigmaEwma` outputs `σ_r` = relative log-return vol **per √second** (sampled on
  the recorded `ts_exchange_ms`, so replay-deterministic). The quoter converts to
  **absolute price vol in ticks per √day**: `σ_p = mid_ticks · σ_r · √86400`, then
  the equations run in tick units (s in ticks, q in base units). Consequence at BTC
  scale: σ_p ≈ 36,000 ticks so σ_p² ≈ 1.3e9, collapsing the usable γ to ~1e-9 — the
  prescribed grid {0.01,0.1,1.0} never fills. See docs/GAMMA_SWEEP.md.
- **κ**: estimate from our own fill data once live: fit λ(δ) = A·exp(−κδ)
  to (quote distance, fill intensity). Until enough fills: κ = 1.5 as a
  documented placeholder.
- **γ**: NOT estimated — it's a preference. Sweep {0.01, 0.1, 1.0} in replay,
  then pick per observed inventory behavior. Document the sweep results.

## Practical modifications (each one is a resume bullet if measured)
1. **Tick rounding & filters**: round bid down / ask up to tickSize; respect
   minNotional and stepSize.
2. **Hysteresis**: only replace a working order if the new price differs by
   ≥ H ticks (H≈1–2) — cuts message rate and rate-limit pressure.
3. **Inventory hard limits**: |q| ≥ q_max ⇒ quote one side only until
   inventory mean-reverts (soft) or kill-switch flatten (hard, at 2·q_max).
4. **Stale/volatile guard**: pull both quotes when book is stale or σ spikes
   beyond a threshold (parameterize; log every trigger).
5. **(Stretch, Phase 8) OFI skew**: shade r by an order-flow-imbalance signal;
   A/B the markout improvement — this becomes the "reduced adverse selection
   by Z%" bullet.

## What to verify in tests
- r and δ formulas against hand-computed fixtures (3+ cases incl. q<0).
- Long inventory ⇒ r < s; higher σ or γ ⇒ wider δ (property tests).
- Unit consistency: doubling σ quadruples the σ² terms.
- Quotes never cross, never violate exchange filters, never exceed risk caps.
