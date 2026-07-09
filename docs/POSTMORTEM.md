# POSTMORTEM — Live run <START_DATE> → <END_DATE>

<!-- Fill after Phase 6. This file is deliberately a template now: knowing what
you'll have to answer keeps the build honest. Interviewers spend more time here
than on the results table. -->

## Run summary
- Symbol/venue, dates, uptime %, restarts (count + causes)
- Final params: γ=…, κ=… (estimated from N fills), τ=…, σ window=…, H=… ticks

## What lost money (minimum 3 entries, be specific)
For each: date/time window → market regime (trend? vol spike? news?) →
what the system did → why the model was wrong there → what would fix it
(and whether the fix is in scope).

Example shape:
> **Aug 14, 09:30–10:15 UTC — trend bleed.** BTC trended +1.8% in 45 min.
> Inventory pinned at −q_max the whole window (we kept selling into the rally);
> one-sided quoting kicked in late because σ-EWMA lagged the regime change.
> Cost: X USDT, ~60% of that week's spread income. Fix candidates: faster σ
> regime detection; OFI skew (Phase 8); tighter q_max.

## What broke operationally
- Every RESYNC trigger with root cause (gap? disconnect? our bug?)
- Rate-limit incidents; stuck-order incidents; clock issues
- The dumbest bug of the project and how it was found

## What the metrics say
- Markout curve interpretation: at which horizon do fills turn against us?
- Fill rate vs quote distance: does our κ fit match reality?
- Latency: where do the p99 outliers come from (log flush? GC-free, so what)?

## Honest limitations
- Testnet fill realism, fee assumptions, single symbol, network RTT floor
- What this system would need before anyone should trust it with real money

## Top 3 things I'd do differently
