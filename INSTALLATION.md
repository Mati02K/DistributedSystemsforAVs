# Installation, build, and running

Everything needed to go from a fresh clone to a verified simulation run.

The stack has three heavyweight dependencies you install yourself (OMNeT++,
SUMO, Bazel) and two that `setup.sh` clones and patches for you (Veins,
ResilientDB). There is no container image and no one-line installer; that is
normal for OMNeT++/SUMO/Veins research code.

At any point, run this to check the toolchain and get a specific fix for
whatever is missing:

```bash
tools/check-prereqs.sh
```

`setup.sh` runs it first, so a missing tool fails immediately with a readable
message rather than as a Bazel or Clang error ten minutes into the build.

---

## 1. Prerequisites

### OMNeT++ 6.2.0

Installed through [`opp_env`](https://github.com/omnetpp/opp_env), which needs
Nix. **Every build and run command in this document assumes you are inside the
`opp_env` shell.**

```bash
# Nix first — this needs sudo, creates a /nix APFS volume and a daemon
sh <(curl -L https://nixos.org/nix/install)
# then open a new terminal so the Nix profile loads

pipx install opp-env          # or: pip install opp-env
mkdir -p ~/omnetpp-ws && cd ~/omnetpp-ws
opp_env init
opp_env install omnetpp-6.2.0
```

Enter the shell (once per session):

```bash
cd ~/omnetpp-ws
opp_env shell omnetpp-6.2.0 --no-build --no-cleanup
```

In a non-login shell, `opp_env` may fail with *"Nix not installed"* even when
Nix is present. Source the daemon profile first:

```bash
source /nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh
```

### SUMO

Veins `5.3.1` — the tag pinned in `third_party/versions.lock` — lists SUMO
`1.8.0`–`1.22.0` as working. **1.20.0 is recommended**: `1.22.0` has a
[reported TraCI connection issue](https://github.com/eclipse-sumo/sumo/issues/17429)
with Veins 5.3.1.

On Linux, `apt install sumo` or the
[DLR PPA](https://sumo.dlr.de/docs/Downloads.php).

**On macOS there is no working package.** SUMO is not in core Homebrew; the
`dlr-ts/sumo` tap fails to load under Homebrew 6.x because every formula in it
calls the removed `cxxstdlib_check`; and the `eclipse-sumo` PyPI wheels link
against `libxerces-c-3.2`, which Homebrew no longer ships (it is on 3.3), while
1.22.0 publishes no macOS wheel at all. Build from source — this exact sequence
is verified on macOS 26, arm64, AppleClang 21:

```bash
brew install cmake xerces-c proj geos
curl -L -o /tmp/sumo-1.20.0.tar.gz \
  https://github.com/eclipse-sumo/sumo/archive/refs/tags/v1_20_0.tar.gz
tar xf /tmp/sumo-1.20.0.tar.gz -C ~ && cd ~/sumo-1_20_0
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target sumo -j"$(sysctl -n hw.ncpu)"

# both are needed — SUMO cannot find its data files without SUMO_HOME
export SUMO_HOME="$HOME/sumo-1_20_0"
export PATH="$SUMO_HOME/bin:$PATH"
```

cmake reports Fox, X11, FreeType, GDAL, Java and SWIG as missing. Those are
only needed for `sumo-gui` and optional extras; the headless `sumo` binary with
TraCI (`--remote-port`), which is all Veins drives, builds without them.

### Bazel

ResilientDB pins Bazel `6.0.0` in its own `.bazelversion` — which only appears
once `setup.sh` clones it into `.external/`. Install
[**bazelisk**](https://github.com/bazelbuild/bazelisk) rather than a raw
`bazel` binary: it reads that file and fetches the pinned version
automatically, instead of you matching it by hand.

```bash
brew install bazelisk        # macOS
```

### OpenSSL headers

```bash
brew install openssl         # macOS
apt install libssl-dev       # Debian/Ubuntu
```

---

## 2. Build

Inside the `opp_env` shell, with `SUMO_HOME` and `PATH` exported:

```bash
./setup.sh
```

That clones and patches both upstreams, builds the bridge, Veins, `libv2vbft`
and the simulation binary, and generates the replica key directories. Cold, it
takes about ten minutes — nearly all of it Bazel compiling ResilientDB's
dependencies. It is idempotent, so re-running skips whatever is already done.

Other modes:

| Command | Effect |
|---|---|
| `./setup.sh` | full: clone, patch, build all four stages, generate keys |
| `./setup.sh --no-build` | materialise `.external/` and keys only, skip compiling |
| `./setup.sh --verify` | check the patches still reproduce `.external/` exactly |

The four build stages, if you want to drive them individually:

| Stage | Command | Produces |
|---|---|---|
| 1. Bridge | `tools/makeres.sh` | `libresdb_omnet_bridge.so` |
| 2. Veins | `cd .external/veins && ./configure && make -j` | `libveins.so` |
| 3. Protocol | `cd src && make -j` | `libv2vbft.so` |
| 4. Simulation | `cd scenarios/fourway && make -j` | `fourway` |

The binaries must be built locally — a committed one carries an rpath to
whichever machine built it.

Changing anything under `bridge/` only needs stages 1 and 4: Bazel reaches it
as `//integration/omnet` through the symlink `setup.sh` creates, and the
protocol links the resulting `.so` rather than including its objects.

Use `tools/makeres.sh` rather than calling `bazel build` directly — it carries
the Clang and Abseil workarounds this build needs (`-xc++`, `-include cstdint`,
`--action_env=PATH`).

**Replica key directories must match the replica count.** The bridge derives N
from `server.config`, and a directory declaring the wrong N runs with no
consensus while reporting nothing obviously wrong. `setup.sh` generates all
eight (`resdb_crypto` = 4 replicas, `_8`, `_12`, `_16`, `_20`, `_24`, `_rb18` =
18, `_rb_units` = 22). To make one by hand:
`tools/gen_crypto_dir.sh <N> scenarios/fourway/<dir>`.

---

## 3. Run

Veins spawns SUMO through `veins_launchd`, which the run script does **not**
start for you. Leave it running in its own terminal — without it the run fails
with a TraCI connection error:

```bash
python3 .external/veins/bin/veins_launchd -vv -c "$SUMO_HOME/bin/sumo"
```

Then, in the `opp_env` shell:

```bash
tools/run-resdb-simulation.sh -u Cmdenv -c FourVehiclesResDB   # headless
tools/run-resdb-simulation.sh -u Qtenv  -c FourVehiclesResDB   # GUI
```

Configs live in `scenarios/fourway/omnetpp.ini`. Useful flags:

| Flag | Effect |
|---|---|
| `--byzleader <id> --leader-byz-type <n>` | make a replica Byzantine; `<n>` is a `ByzantineType` from `src/v2vbft/protocol/Primitives.h` |
| `--no-firewall` | disable the f+1 pre-verification checks |
| `--randomize <N> <F>` | generate a random scenario |
| `--rollback-late-emergency` | late-ambulance rollback scenario |
| `--tolerated-f <n>` | override the tolerated fault count |

Both value-taking flags need their value; passing `--byzleader` bare silently
consumes the next argument.

Full log goes to `/tmp/resdb-simulation.log`, or `$LOG_FILE` if set.

---

## 4. Check the install is good

Exit code alone is not enough — check that consensus actually happened. For
`FourVehiclesResDB`:

```bash
export LOG_FILE=/tmp/resdb-verify.log
tools/run-resdb-simulation.sh -u Cmdenv -c FourVehiclesResDB

for m in ProposeAll_Submit_Time Order_Decided_Time Batch_Assignment \
         Resume_Time DISCOVERY-COMPLETE; do
  printf '%-24s %s\n' "$m" "$(grep -c "$m" "$LOG_FILE")"
done
```

A healthy run ends with `RUN EXIT: 0` and:

| Marker | Expected | Meaning |
|---|---|---|
| `ProposeAll_Submit_Time` | **1** | only the cert-primary proposes |
| `DISCOVERY-COMPLETE` | **4** | every replica closed its own discovery view |
| `Order_Decided_Time` | **4** | every replica committed the order |
| `Batch_Assignment` | **4** | every vehicle got a crossing batch |
| `Resume_Time` | **4** | every vehicle crossed |

Anything other than exactly one `ProposeAll_Submit_Time` means leader election
misbehaved. Counts below the replica count mean replicas failed to commit.

The `veins_launchd` log should show it spawning your SUMO, which confirms the
whole chain (SUMO → TraCI → Veins → the protocol → ResilientDB PBFT):

```text
Starting SUMO ($HOME/sumo-1_20_0/bin/sumo -c bft_4veh.sumo.cfg) on port ...
```

For a deeper check across repetitions, use the structural harness described in
the README's *Refactoring safely* section (`tests/golden/run_golden.py`). A full
matrix is roughly 55 minutes.

---

## 5. Troubleshooting

**`OMNETPP_ROOT is not set`** — you are outside the `opp_env` shell.

**`opp_env`: "Nix not installed"** — source the daemon profile:
`source /nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh`.

**`libveins.so` / `libv2vbft.so`: cannot open shared object file** — rebuild
stages 2–4 locally.

**`Bus error: 10` (exit 138), stack trace through `SandboxChecker` and
`SecStaticCodeCreateWithPath`, before the first event** — the loader path is
shadowing the macOS system frameworks. `run-resdb-simulation.sh` sets
`DYLD_FALLBACK_LIBRARY_PATH` for this reason; a checkout that still exports
`DYLD_LIBRARY_PATH` there crashes this way on macOS 26. Note it is Qt faulting
even under `-u Cmdenv`, because the simulation binary links `liboppqtenv`.

**TraCI connection refused / the run hangs at startup** — `veins_launchd` is
not running, or was started without `-c` pointing at your `sumo` binary.

**Exit 139 / 133 on 18-vehicle runs** — a known TraCI crash when a node queries
a departed vehicle after sim overrun. It fires *after* the commit, so metrics
are unaffected; the golden harness judges the invariants, not the exit code.

**Bazel fails on Boost `int_float_mixture_enum`, or `<cstdint> file not
found`** — your Clang is newer than the pinned Boost expects. `tools/makeres.sh`
already passes the necessary suppressions and `-xc++`; use it rather than
calling `bazel build` directly.

**`cd src && make` suddenly fails with `undefined symbol:
ResdbOmnetCreateKvServer`** (and a dozen sibling `ResdbOmnet*` symbols) after a
build that previously worked — the bridge library has vanished, not your code.
`tools/makeres.sh` defaults `BAZEL_OUTPUT_ROOT` to `/tmp/bazel`, and macOS
prunes `/private/tmp`, so `.external/resilientdb/bazel-bin` is left dangling at
a path that no longer exists. Confirm with:

```bash
ls -l .external/resilientdb/bazel-bin/integration/omnet/libresdb_omnet_bridge.dylib
```

Rebuild with `tools/makeres.sh` (~8 min cold). To stop it recurring, point the
cache somewhere durable — the tradeoff is disk, and it is the one variable this
build is otherwise known to work with:

```bash
BAZEL_OUTPUT_ROOT="$HOME/.cache/bazel-v2vbft" tools/makeres.sh
```

Note the failure is a *link* error, so the compiler will have reported your
sources as fine — the missing library is the whole cause.

**Patch fails to apply during `setup.sh`** — the pinned upstream revision moved
or your `.external/` is dirty. Delete the offending tree under `.external/` and
re-run.
