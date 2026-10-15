# Birth charts and the sky through the matrix model (redesign 9.7)

1000 bred heirs (generation 30, 200 per strategy), 32 runs each, K=42, the adopted final trait model.

## Natal charts on (no sky)

| strategy | clear, no charts | clear, charts | delta |
|---|---|---|---|
| random | 56.1% | 56.9% | +0.8 |
| heavy | 56.8% | 57.5% | +0.7 |
| blink | 58.7% | 59.4% | +0.7 |
| fire | 59.8% | 60.9% | +1.1 |
| power | 58.5% | 59.2% | +0.7 |
| **all** | 58.0% | 58.8% | +0.8 |

## Does any chart dominate? (same heirs, charts off vs on)

| group | heirs | off | on | delta |
|---|---|---|---|---|
| Sun fire (HOT) | 227 | 56.2% | 57.1% | +0.9 |
| Sun earth (STONE) | 269 | 58.0% | 58.5% | +0.5 |
| Sun air (AIRY) | 234 | 58.0% | 59.0% | +0.9 |
| Sun water (WET) | 270 | 57.9% | 58.8% | +0.9 |
| Mars fire (HOT) | 224 | 57.2% | 57.7% | +0.5 |
| Mars earth (STONE) | 256 | 58.0% | 59.0% | +0.9 |
| Mars air (AIRY) | 238 | 57.7% | 58.4% | +0.6 |
| Mars water (WET) | 282 | 57.3% | 58.3% | +1.1 |
|  no alignment | 413 | 58.1% | 58.8% | +0.7 |
|  one tier | 374 | 57.1% | 57.9% | +0.8 |
|  optimiser at the cap | 213 | 57.4% | 58.4% | +1.0 |

Across the 16 Sun x Mars element pairs: best +1.5 (Sun fire (HOT), Mars fire (HOT) (50 heirs)), worst -0.4 (Sun earth (STONE), Mars fire (HOT) (60 heirs)).
Single heirs: the biggest gain is +16 points over 32 runs (noise floor at 32 runs is about +-9); 84 of 1000 gain more than 5.

## Sky states, each forced on every run (charts on, no transits; vs charts on, no sky: 58.8%)

| state | clear | delta | bound |
|---|---|---|---|
| Mercury retrograde | 57.8% | -1.0 | ok (<= 5) |
| Venus retrograde | 58.8% | +0.0 | ok (<= 5) |
| Mars retrograde | 59.9% | +1.1 | ok (<= 5) |
| Jupiter station | 58.9% | +0.1 | ok (<= 5) |
| Saturn station | 56.9% | -1.9 | ok (<= 5) |
| solar eclipse | 58.6% | -0.2 | ok (<= 5) |
| lunar eclipse | 56.0% | -2.8 | ok (<= 5) |
| full moon | 57.9% | -0.9 | ok (<= 5) |
| new moon | 58.8% | +0.0 | ok (<= 5) |
| super full moon | 56.1% | -2.7 | ok (<= 5) |
| void-of-course moon | 57.8% | -1.0 | ok (<= 5) |
| solstice or equinox | 58.8% | +0.0 | ok (<= 5) |
| solar return (birthday) | 61.0% | +2.2 | ok (<= 5) |
| lunar return | 58.8% | +0.0 | ok (<= 5) |
| meteor shower | 58.8% | +0.0 | ok (<= 5) |
| worst stack: Mercury Rx + Saturn station + super full moon | 55.1% | -3.7 | ok (<= 5) |
| best stack: Jupiter station + birthday + new moon | 61.1% | +2.3 | ok (<= 5) |

## Real moments, 2026-01-01 .. 2030-12-31 every 12 h (3652 moments; 200 heirs x 8 runs each, the same 8 runs as its baseline; full sky + transits)

Clear-rate change against charts-on-no-sky (1600 runs a moment, so about +-2.5 points of it is sampling noise):

| p5 | p25 | median | p75 | p95 | min | max | moments within +-2 |
|---|---|---|---|---|---|---|---|
| -1.6 | -1.0 | -0.3 | +0.1 | +0.6 | -5.0 | +2.1 | 97.2% |

How often each state held (share of all 7304 six-hour moments): Mercury Rx 19.4%, Venus Rx 6.9%, Mars Rx 8.8%, Jupiter station 1.0%, Saturn station 1.0%, solar eclipse 0.7%, lunar eclipse 0.7%, full moon 12.4%, new moon 12.4%, super 1.4%, void of course 28.4%, season day 12.5%, meteor shower 4.5%.

All heirs, runs on seeded moments across 2026-2030 (32 runs each): 58.3% (charts, no sky 58.8%; no charts 58.0%).

| strategy | no charts | charts + sky | delta |
|---|---|---|---|
| random | 56.1% | 56.5% | +0.4 |
| heavy | 56.8% | 57.1% | +0.3 |
| blink | 58.7% | 58.9% | +0.2 |
| fire | 59.8% | 60.3% | +0.5 |
| power | 58.5% | 58.8% | +0.3 |

## SKYBORN: event-born heirs (charts on, no sky; vs the same heirs without the mark: 58.8%)

Each kind forced on every heir (a player who times every birth to that event), and the bound of +-3:

| kind | trait | clear | delta | bound |
|---|---|---|---|---|
| Halley-born | a wish star from 1 elite in 20 (offering +1) | 58.8% | +0.0 | ok (<= 3) |
| eclipse-born | 1 more hidden SHINY cell in dark rooms (dark x15/16) | 58.8% | +0.0 | ok (<= 3) |
| ingress-born | 1 guided talk a run (no numbers) | 58.8% | +0.0 | ok (<= 3) |
| retro-born | the last room's recipe hint once a floor (mix x13/16) | 59.4% | +0.6 | ok (<= 3) |
| moon-born | +1 status | 58.8% | +0.0 | ok (<= 3) |
| meteor-born | a wish star from 1 elite in 20 (offering +1) | 58.8% | +0.0 | ok (<= 3) |
| season-born | +1 guard | 59.7% | +0.9 | ok (<= 3) |

Largest forced move: +0.9 points.

Natural births (each heir's own seeded minute, 2026..2126; the windows of 19.1, rarest first):

| kind | heirs | without | with | delta |
|---|---|---|---|---|
| none | 823 | 58.2% | 58.2% | +0.0 |
| Halley-born | 0 | 0.0% | 0.0% | +0.0 |
| eclipse-born | 14 | 59.4% | 59.4% | +0.0 |
| ingress-born | 14 | 58.2% | 58.2% | +0.0 |
| retro-born | 45 | 62.1% | 62.8% | +0.7 |
| moon-born | 58 | 57.2% | 57.2% | +0.0 |
| meteor-born | 39 | 60.3% | 60.3% | +0.1 |
| season-born | 7 | 54.6% | 56.3% | +1.7 |

Sky-born share: 177 of 1000 heirs (17.7%); their clear rate 59.3% -> 59.6% (+0.3). All heirs: 58.8% -> 58.8% (+0.0).

## Siblings: the Moon and the Rising across the three kids (20000 sets, death minutes seeded over 2026..2126)

| rule | span, median / p95 / max (min) | Risings all 3 signs | Rising gap, min / median (deg) | Moon first->last, median / p95 (deg) | Moon signs 1 / 2 / 3 | probes, mean / max |
|---|---|---|---|---|---|---|
| old: 7..97 min apart | 104 / 166 / 194 | 9.8% | 1.4 / 7.8 | 0.94 / 1.55 | 96.8% / 3.2% / 0.0% | 0.0 / 0 |
| new: Rising steps + Moon catch | 218 / 327 / 546 | 100.0% | 9.3 / 21.4 | 1.98 / 3.15 | 86.4% / 13.6% / 0.0% | 14.5 / 23 |
