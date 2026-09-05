# Ablation studies — what each figure shows, and why it looks that way

Generated from the run of **2026-09-01**: all seven studies, 3 repetitions, 417 runs, one consistent sweep. Regenerate with, inside
`opp_env` and with `veins_launchd` on :9999 (see
[../INSTALLATION.md](../INSTALLATION.md)):

```bash
benchmarks/ablations/run_ablations.sh <1..7|all> [reps]   # -> benchmarks/ablations/results/
python3 -m plotter build-all                              # -> figures/
python3 -m plotter summary                                # -> figures/results.md
```

## Scenario, in one paragraph

Four approaches, **one lane each**, no turn pockets. Every vehicle draws its exit
uniformly from its approach's three valid exits (`<routeDistribution>`), so
roughly a third turn left; the U-turn is absent from the distribution, so nobody
leaves the way they came. `*.manager.seed` is set per repetition, so each rep
samples a different turn assignment. All vehicles depart at `t=0` — a single
arrival burst, not continuous demand, because the protocol currently completes
**one consensus epoch** per run.

Min–max ranges are no longer drawn on any figure. The spread is still computed
and printed per cell in [`figures/results.md`](../figures/results.md).

---

## 1. RSU units — [ab1_rsu.png](../figures/ab1_rsu.png)

Three panels: throughput, stop-to-clearing delay, message cost. Vehicles 4–20,
with and without 4 static intersection units, **all at k=0 (no faults)**.

**Throughput and delay are identical between arms. That is the correct result.**
RSU units are non-vehicle replicas: they vote, echo and relay, but they never
queue at the junction and are never scheduled. The same vehicles cross in the
same conflict-matrix batches either way, so per-vehicle delay and throughput
cannot move. Only traffic changes — 63 → 135 msgs/vehicle at N=4, 326 → 459 at
N=20.

**The benefit is real but is not on this figure.** Every panel is pinned to
`k=0`, the one condition where units provably cannot help. The fault frontier,
measured in the same 186 logs, is where they earn their keep:

| | OFF | ON |
|---|---|---|
| N=4 | dies at k=2 | survives k=2 |
| N=8 | dies at k=4 | survives k=4 |
| N=16 | dies at k=6 | survives k=6 |
| N=20 | dies at k=7 | survives k=7, 2/3 at k=8 |

RSUs buy **1–2 additional tolerated silent replicas** at every vehicle count.
Quorum confirms they are genuine members: 3→5 at N=4, 13→15 at N=20, matching
`2f+1` over the enlarged membership.

**Open:** N=12 goes the wrong way (OFF `k5:2/3`, ON `k5:1/3`) where every other
count improves. One cell against four.

**To fix the figure:** add a commit-rate-vs-k panel. The data already exists; no
re-run needed.

**Earlier versions of this figure were wrong.** Before 2026-08-27 the ON arm
showed a large delay penalty (≈18s vs 6s at N=4). That was the `toleratedF`
defect below, not a property of RSUs — the arm was falling back to stop-sign
timeouts because consensus never committed.

---

## 2. Fake-ambulance attack — [ab2_attack.png](../figures/ab2_attack.png)

**This study currently measures nothing. Do not present it.**

Colluders take the *highest* replica ids, so `CertPrimary()` — which elects the
smallest certified id — always picks an honest proposer. That isolates the
certificate gate from leader succession, which is what we want. Arms are
`enableAmbulanceCertGate` on vs off.

The gate now fires correctly (90 rejections in the gate arm, 0 in the control).
But in the **control** arm, where the certless claim is deliberately allowed
through, the liars still gain nothing — they wait ~0.9s *longer* than honest
vehicles, and both arms grant zero false priority.

So the attack does not succeed even when undefended. The lie is accepted at the
announce layer and never becomes priority at the scheduler: `e.is_ambulance` is
set from the proposer's `local_vehicle_states_`
([ResDBDecision.cc:146](../src/v2vbft/app/ResDBDecision.cc)) and the scheduler
prioritises on `is_ambulance && cyber_status == 1`, but something between those
points drops it. Until that path is traced, both arms will be flat regardless of
the gate.

---

## 3. Actuated traffic light vs consensus — [ab3_baseline.png](../figures/ab3_baseline.png)

**These delay numbers are not reportable yet — the two arms start their clock
37.8 m apart.** Ours triggers at `stopDistance = 5m` measured as
`laneLength - lanePosition`, and with 292.80 m approach lanes into a junction at
(300,300) the lane ends 7.2 m from centre, so our clock starts at 12.2 m. The
`tl*veh` configs never set `stopDistance`, so the signal arm inherits
`BaselineModule.ned`'s 50 m default, Euclidean to the centre. `BaselineModule`
also backdates `stopTime = arrivalTime` for any vehicle reaching its 20 m
`departDistance` still rolling above 0.5 m/s, which fired for **484 of 540
(89.6%)** signal vehicles — so for roughly nine in ten the metric is literally
"50 m mark to cleared", approach travel included. The signal's per-vehicle floor
is flat at 4.1–4.6 s at every load, which is a transit time rather than a wait;
ours is 0.80 s at N=8. Correcting for the offset moves the ratios below to about
1.48/1.53/1.30/1.29×. The departure trigger is already identical in both arms, so
the start is the only asymmetry. Fix is one line per `tl*veh` config
(`stopDistance = 12.2m`) and a re-run of the signal arm only. Panels 1, 3 and 4
are affected; service rate is departure-instant based and is not.

| N | signal wait | ours | delay | service rate |
|---|---|---|---|---|
| 8 | 14.40s | 7.28s | 1.98× | 3.42× |
| 12 | 18.81s | 9.91s | 1.90× | 2.51× |
| 16 | 18.22s | 11.26s | 1.62× | 2.77× |
| 20 | 20.97s | 13.49s | 1.55× | 2.71× |

**Why a signal and not an all-way stop.** AIM surveys report fixed-time
signalling as the most-used baseline (46%) while explicitly calling for actuated
or adaptive instead; all-way stop control is used by only 8%. The all-way stop
was also unusable for a second reason — it **deadlocked in 7 of 12 runs** under
mixed turn demand, resolved only by SUMO's 300s teleport timer, producing
300–600s "waits" that are jam-resolution artifacts and flatten every real arm to
the axis floor.

**Why actuated and not fixed-time.** This scenario is a single arrival burst. A
fixed cycle would burn green on approaches that emptied and never refill, losing
for a reason unrelated to signal control. Actuated extends green while vehicles
are present and ends the phase on a gap, so it serves a burst on its merits.

**Why the gap narrows with load** (1.98× → 1.55×): the signal amortises its
fixed phase overhead better as the queue grows, while our per-round consensus
cost is roughly constant. Extrapolating past N=20 from four points is not
supported.

**Protected left turns are impossible here.** Each approach has a single lane, so
through and left share it and no phase can hold one red while the other runs.
`--tls.left-green.time` was verified to produce a byte-identical program. This
constrains the signal exactly as it constrains our protocol.

---

## 4. Ambulance priority — [ab4_priority.png](../figures/ab4_priority.png)

**9 repetitions**, not 3. The ambulance is *one vehicle*, so this study samples
n=1 per run where the others average 8–20; at 3 reps its wait swung 3.4s between
repetitions, wider than the whole N=8→20 trend.

The ambulance's route is also **pinned** to its approach's concrete straight
route while every other vehicle stays random. Leaving the measured vehicle's turn
to a coin flip let its own draw dominate the metric — a left-turning ambulance
batches only with the opposing left, a straight one batches freely.

| N | priority | FIFO | benefit |
|---|---|---|---|
| 8 | 7.29s | 10.34s | 3.05s |
| 12 | 7.36s | 13.93s | 6.57s |
| 16 | 8.97s | 15.72s | 6.75s |
| 20 | 9.87s | 18.52s | 8.65s |

Monotone in all four series, and **the benefit grows with load** — which is the
claim the title makes. Priority wait rises gently (7.29 → 9.87) while FIFO rises
steeply (10.34 → 18.52): the ambulance is inserted near the front regardless of
queue length, so what grows is what it skips.

**"Cost to everyone else" excludes the priority vehicle.** Averaging it into the
figure that reports what priority cost the others let its own saving cancel part
of the cost it caused; at N=8 that reversed the sign, showing -0.09s (priority
reads as free) where the honest figure is +0.33s. Corrected costs are +0.33 /
+1.15 / +2.22 / +2.53s per other vehicle at N=8/12/16/20.

**Open, same defect one panel over:** the *relative* panel still divides by a
mean that includes the priority vehicle, which biases every point toward 1.0 --
about 0.06-0.10 at N=8, under 0.02 by N=20. It understates the effect in both
arms rather than flattering it. Also note the N=8 priority cells are bimodal
across repetitions (0.11, 0.11, 0.64, 1.38, 1.46, 1.59, 1.98 excluding the
vehicle itself): the priority car either makes the first conflict-matrix batch or
waits a full round, with nothing in between, so the plotted N=8 mean describes no
run that actually happened. That column rests on 7 repetitions, not 9.

The *"relative to the rest of the traffic"* panel is the honest headline — it
divides by the fleet mean, cancelling run-level variation.

---

## 5. Late-emergency rollback — [ab5_rollback.png](../figures/ab5_rollback.png)

| | cleared | wait | msgs/vehicle |
|---|---|---|---|
| rollback OFF | 16.0 | 10.40s | 357 |
| rollback ON | 16.0 | 12.30s | 554 |

**Both arms clear the same number of vehicles.** Rollback does not serve *fewer*;
it serves *different ones* — the ambulance takes a slot an ordinary vehicle would
have had, and the rest shift later. Every count-based summary shows the arms as
identical, which is why the left panel plots individual departures rather than a
total.

Cost is ~1.9s of extra wait and ~1.55× messages: revising a committed order means
a CANCEL round on top of the ORDER round.

**Open:** rollback fired in 2 of 3 reps in the latest run, 3 of 3 previously.
With 3 reps a regression cannot be separated from the turn draw. This is the
cheapest study (~5 min for 3 reps) — it should be run at 9–12.

---


## 6. Imperfect perception — [ab6_perception.png](../figures/ab6_perception.png)

Three panels: certificate-collection time, commit rate, vehicles cleared.
Lateral observation sigma 0 to 2.0 m, N = 4 to 20, 3 repetitions, all replicas
honest.

This study exists because the arrival gate stopped being an oracle. A witness
used to ask TraCI where a vehicle really was; it now endorses a claim only if
its own observation agrees ([ResDBArrivalProtocol.cc](../src/v2vbft/app/ResDBArrivalProtocol.cc)).
That is what makes a lane lie catchable by sensing rather than by consulting the
simulator — and it also means an honest vehicle can be refused because its
witness misread it.

**Sigma is a physical quantity, not a knob.** Each arm's confusion matrix is the
one implied by a Gaussian lateral error of that sigma against the junction's real
3.2 m lane width, generated by
[generate_perception_matrices.py](../scenarios/fourway/generate_perception_matrices.py).
The generator reproduces the checked-in catalog byte-for-byte, so the x axis
cannot drift from the geometry it claims to describe.

**There is a frontier at sigma ≈ 0.5 m, and past it consensus fails.**

| commit rate | σ=0 | 0.25 | 0.5 | 1.0 | 2.0 |
|---|---|---|---|---|---|
| N=4 | 100 | 100 | 100 | 100 | 100 |
| N=8 | 100 | 100 | 100 | 33 | 0 |
| N=12 | 100 | 100 | 100 | 33 | 33 |
| N=16 | 100 | 100 | 100 | 100 | 0 |
| N=20 | 100 | 100 | 100 | 0 | 0 |

Below 0.5 m the cost is paid entirely in collection latency and every
configuration still commits. Above it the protocol stops working, and **scale
makes it worse, not better**: more witnesses means more vehicles that must each
reach `f+1` agreement, so the chance that at least one cannot rises with N.

**An earlier version of this figure claimed the opposite.** Built from an N=4,
1-repetition slice, it said perception error was absorbed by certificate
collection before reaching consensus. That is true at N=4 — which is the only
configuration where it is true — and the full sweep contradicts it everywhere
else. The caption has been corrected.

**Do not read the crossings panel as success.** It stays high even where the
commit rate is zero, because the stop-sign timeout releases vehicles consensus
never scheduled. That is the safety fallback doing its job, not the protocol
doing its job; only the middle panel distinguishes them.

**Open:** at 3 repetitions the frontier is located but its shape is not. N=16
survives sigma 1.0 while N=8 and N=12 do not, which is sampling rather than a
real inversion. Resolving the frontier per N needs more repetitions.

---

## 7. One lane vs two lanes — [ab7_twolane.png](../figures/ab7_twolane.png)

Three panels: schedule length, throughput, delay. N = 4, 8, 16, 20, 3
repetitions, all honest, both arms on the same routes and seeds.

The scheduler used to refuse to batch any two vehicles from the same approach.
With one lane that is right — one is behind the other. With two it discards the
reason the lane exists: an inner-lane left-turner and an outer-lane
straight-goer enter from different points and leave by different roads, which is
exactly the pair a signalised junction runs in one phase. The rule now permits
that pair and only that pair
([resdb_conflict_matrix.h](../bridge/resdb_conflict_matrix.h)).

| N | batches 1L → 2L | throughput 1L → 2L | delay 1L → 2L |
|---|---|---|---|
| 4 | 3.7 → 4.0 | 0.50 → 0.48 | 6.04 → 5.82 |
| 8 | 5.7 → **4.0** | 0.61 → **0.79** | 7.38 → **6.95** |
| 16 | 10.3 → **7.0** | 0.73 → **0.93** | 11.54 → **9.96** |
| 20 | 12.7 → **10.0** | 0.73 → 0.79 | 13.90 → 14.48 |

**The schedule gets shorter at every load above N=4, by roughly a third.**
30% fewer batches at N=8, 32% at N=16, narrowing to 21% at N=20: with more
vehicles per approach, the queue behind each pair comes to dominate the saving
from running the pair together. Throughput follows the same shape.

An earlier version of this table reported 48% at N=8 with throughput doubling.
That was measuring a broken one-lane baseline, not the second lane — see the
N=8 defect below.

**N=4 is a wash, and should be.** One vehicle per approach means there is rarely
a same-approach pair to run in parallel, so the second lane has nothing to
exploit. Two lanes are slightly *worse* on batches (4.0 vs 3.7) and throughput
(0.48 vs 0.50), within noise.

**Delay crosses over at N=20** — 14.48 s against 13.90 s, the one cell where the
two-lane arm loses. Not explained yet. The stop-zone geometry is corrected
(both arms trigger the same distance from the junction centre, see
`stopDistance` in the two-lane configs), so it is not the confound that
invalidates ablation 3. Most likely the longer per-lane queues interact with the
clearance-gated release, but that is a hypothesis, not a finding.

**This does not show two lanes are safer.** They are not: more parallelism means
more vehicles in the conflict box at once, and the protection is the conflict
matrix rather than the geometry. The scheduler re-tests every batch it emits
against its own safety predicate and logs `[SCHEDULER-UNSAFE-BATCH]`; that
marker is absent from all 24 runs behind this figure, and the claim is "more
throughput at equal safety" only while it stays absent.

---

## Cost decomposition — [an_cost_decomposition.png](../figures/an_cost_decomposition.png)

Honest operation only (`k=0`); with replicas silenced the traffic mix reflects
failure, not normal composition.

**Per occurrence, certificate formation is the slower of the two** — 199ms vs
193ms at N=16, and the gap is far wider at low load (110 vs 71ms at N=4, 199 vs
81ms at N=8). It is not slower because it does more work: it is one round trip
at an `f+1` threshold against PBFT's two to three phases at `2f+1`. It is slower
because it *waits*. A vehicle re-announces until `f+1` peers echo it, and those
peers have to physically arrive at the intersection first, so completion is
bounded by the announce retransmission interval rather than by transmission
time. PBFT's phases fire back to back with nothing to wait for.

**Per occurrence the message counts cross over.** Certificates cost more per
event at low load (52 vs 29 at N=4, 86 vs 53 at N=8) and *less* from N=12 on
(130 vs 164, 204 vs 211, 275 vs 325) — a PBFT round grows with the replica
count while one vehicle's certificate does not.

**In total traffic certificates still dominate ~15.5×** (3258 vs 211 messages at
N=16), because the cert layer runs once per *vehicle* while PBFT runs once per
*round*. Which layer is "expensive" therefore depends entirely on whether the
question is per event or per run, which is why the figure normalises per
occurrence and this note states the totals.

Certificates are normalised **per vehicle** and PBFT **per round**, so the left
panel compares one occurrence against one occurrence. Comparing raw totals would
pit a layer that ran N times against one that ran once.

Two things changed the picture recently and are worth knowing:

- **Discovery no longer waits out a fixed timer.** Epoch 0 used to be barred from
  closing early, so every round burned the full `certCollectionTimeoutSec = 8s`
  even with every certificate already collected. It now closes once the roster is
  certified.
- **Announces back off exponentially** (0.1s → 3.2s, reset when a new peer is
  heard). A vehicle re-announces until `f+1` peers echo it, which takes the whole
  approach; at a fixed 100ms that was ~130 announces each, and every listener
  echoes every announce it hears.

---


## Defects found and fixed in this cycle

**`f` was computed two ways.** Certificate *formation* derived `f` from
`total_vehicles_`; *validation* used `toleratedF()`, i.e. `num_replicas_`
including RSU units. With units present these never agree, so every certificate
was assembled one to two echoes short of what every receiver demanded and **every
replica dropped every other replica's certificate**. Each vehicle kept only its
own → a proposal that is mostly QUIET → no commit → stop-sign fallback.

Intermittent rather than constant, because extra echoes sometimes arrive before
the certificate latches — which is why it presented as "N=4 instability". It
explained ab1's phantom RSU delay penalty *and* ab6's S4/S5 attack cells, which
decided nothing in every rep and now decide normally.

Three further sites used the same wrong formula and were unified on
`toleratedF()`: gossip propagation confirmation, consensus relay carrier
threshold, and a Byzantine-injection log line.

## Known-open issues

- **ab3 measures the two arms from different start points** — 37.8 m of
  approach charged only to the signal (above); delay and throughput are not
  reportable until the `tl` arm is re-run
- **ab2 measures nothing** — attack does not succeed even undefended (above)
- **ab1 does not plot the fault frontier**, which is the RSU benefit
- **ab1 N=12** fault tolerance goes the wrong way with units
- **ab5 needs more repetitions** — rollback fired 2/3 vs 3/3 across runs
- **Single-lane approaches** preclude protected-left phases for every arm
- **Burst arrival, single epoch** — no steady-state comparison is possible until
  multi-epoch support lands
