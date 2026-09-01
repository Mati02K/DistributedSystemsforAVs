"""Ablation 7 — what a second lane per approach buys.

The scheduler used to refuse to batch any two vehicles arriving from the same
approach. With one lane that is right: one is behind the other. With two it
throws away the reason the lane exists — an inner-lane left-turner and an
outer-lane straight-goer enter from different points and leave by different
roads, which is exactly the pair a signalised junction runs in one phase.

Three panels, which together separate "more parallel" from "faster":

  batches -> throughput -> delay

Batch count is the direct consequence of the scheduling rule and the one number
that cannot be explained by anything else. Throughput and delay are what that
buys in traffic terms, and they can move for reasons the rule has nothing to do
with, so they are read second.

The two arms are NOT the same physical world. The two-lane junction has a larger
radius, so its approaches are 3.2 m shorter and its lanes end further from the
centre. Left uncorrected, every vehicle would begin its stop zone 3.2 m further
out than in the one-lane arm and the delay panel would be measuring geometry.
The two-lane configs therefore set stopDistance = 5 - 3.2/(N/2), so both arms
trigger the same distance from the junction centre. This is the same confound
that already invalidates ablation 3's delay numbers; it is corrected here rather
than noted.

What is deliberately NOT claimed: that two lanes are safer. They are not — more
parallelism means more vehicles in the conflict box at once, and the protection
is the conflict matrix, not the geometry. The scheduler re-tests every batch it
emits against its own safety predicate and logs [SCHEDULER-UNSAFE-BATCH]; the
correct reading of this figure is "more throughput at equal safety", and that
holds only while that marker stays absent from the logs behind it.
"""
from ..io import discover
from ..metrics import aggregate
from .. import style

NAME = "ab7_twolane"
TITLE = "One lane vs two lanes per approach: parallelism, throughput and delay"
STUDY = 7
SUBPLOTS = (1, 3)
FIGSIZE = (15.0, 5.0)

_ARMS = (("one", "control", "one lane"),
         ("two", "treatment", "two lanes"))


def _batches(recs):
    """Mean committed batch count.

    Reported straight from the order header rather than derived, and taken only
    from runs that committed: a collapsed run has no schedule, and averaging its
    absent batch count as zero would read as perfect parallelism.
    """
    vals = [r.n_batches for r in recs if r.committed and r.n_batches is not None]
    return aggregate.summarize(vals)


_PANELS = (
    ("Schedule length", "batches per epoch", _batches),
    ("Throughput", "vehicles cleared / s", aggregate.throughput),
    ("Delay", "mean seconds per vehicle", aggregate.clearance_wait),
)


def load(runs):
    ns = discover.ns(runs, arm="one", study=STUDY)
    if not ns:
        return {}
    data = {}
    for arm, role, label in _ARMS:
        cells = [(n, discover.cell(runs, STUDY, arm, n=n)) for n in ns]
        cells = [(n, recs) for n, recs in cells if recs]
        if cells:
            data[arm] = (role, label, cells)
    return data


def build(data, axes):
    for ax, (title, ylabel, metric) in zip(axes.flat, _PANELS):
        for arm, role, label in _ARMS:
            if arm not in data:
                continue
            _, lbl, cells = data[arm]
            xs = [n for n, _ in cells]
            stats = [metric(recs) for _, recs in cells]
            style.errorbar(ax, xs, stats, style.series(role, lbl),
                           dashed=(role == "control"))
        style.finish(ax, title=title, xlabel="vehicles", ylabel=ylabel,
                     legend=True)

    fig = axes.flat[0].get_figure()
    fig.suptitle(
        "Ablation 7 — the second lane shortens the schedule; the conflict "
        "matrix, not the geometry, keeps it safe",
        fontsize=12, color=style.INK_PRIMARY)
