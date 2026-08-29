#!/usr/bin/env bash
# check-prereqs.sh — verify the toolchain before setup.sh spends ~10 minutes
# building and then fails mid-way with a cryptic Bazel/Clang error.
#
# Checks presence, and where a version is actually known to matter, the
# version, of every tool this project assumes is already installed:
# opp_env/OMNeT++, SUMO, Bazel, OpenSSL headers, git, make.
#
#   tools/check-prereqs.sh
#
# Exits non-zero only on a hard failure (something missing). Version
# mismatches that might still work are reported as warnings, not failures.
set -uo pipefail   # not -e: run every check and report all of them, not just the first

say()  { printf '\n\033[1m%s\033[0m\n' "$*"; }
ok()   { printf '  \033[32mOK\033[0m    %s\n' "$*"; }
warn() { printf '  \033[33mWARN\033[0m  %s\n' "$*"; WARNED=1; }
fail() { printf '  \033[31mFAIL\033[0m  %s\n' "$*"; FAILED=1; }

FAILED=0
WARNED=0

say "OMNeT++ / opp_env"
if [[ -z "${OMNETPP_ROOT:-}" ]]; then
  fail "OMNETPP_ROOT is not set. Install opp_env (needs Nix, then 'pip install opp-env'), then:"
  echo "        opp_env shell -w <workspace> omnetpp-6.2.0 --no-build --no-cleanup"
elif [[ ! -x "${OMNETPP_ROOT}/bin/opp_run" ]]; then
  fail "OMNETPP_ROOT=${OMNETPP_ROOT} but bin/opp_run is missing — looks like a broken or partial install."
else
  # opp_run -v prints a copyright banner first; the version is on a later line.
  ver="$("${OMNETPP_ROOT}/bin/opp_run" -v 2>/dev/null | grep -m1 '^Version:' || true)"
  if [[ -z "$ver" ]]; then
    ok "OMNETPP_ROOT=${OMNETPP_ROOT} (opp_run present; couldn't read a version string to confirm 6.2.0)"
  elif [[ "$ver" == *6.2* ]]; then
    ok "OMNETPP_ROOT=${OMNETPP_ROOT} (${ver})"
  else
    warn "OMNETPP_ROOT=${OMNETPP_ROOT} — this repo is developed against 6.2.0, detected: ${ver}"
  fi
fi

say "SUMO"
if ! command -v sumo >/dev/null 2>&1; then
  fail "sumo not found on PATH. Recommended: 1.20.0 (Veins 5.3.1 supports 1.8.0-1.22.0, see https://veins.car2x.org/download/)."
else
  ver="$(sumo --version 2>/dev/null | head -1 || true)"
  ok "sumo on PATH ($(command -v sumo)) — ${ver:-version unknown}"
  if [[ "$ver" == *1.22.0* ]]; then
    warn "SUMO 1.22.0 has a reported TraCI connection issue with Veins 5.3.1 (github.com/eclipse-sumo/sumo/issues/17429). If veins_launchd fails to connect, try 1.20.0."
  fi
  # A source-built SUMO runs but cannot find its data files without this.
  if [[ -z "${SUMO_HOME:-}" ]]; then
    warn "SUMO_HOME is not set. Set it to the SUMO install/source root, e.g. export SUMO_HOME=\"\$HOME/sumo-1_20_0\"."
  elif [[ ! -d "${SUMO_HOME}" ]]; then
    warn "SUMO_HOME=${SUMO_HOME} does not exist."
  else
    ok "SUMO_HOME=${SUMO_HOME}"
  fi
fi

say "Bazel"
if command -v bazelisk >/dev/null 2>&1; then
  ok "bazelisk on PATH ($(command -v bazelisk)) — will auto-select the Bazel version ResilientDB pins in its own .bazelversion (6.0.0) once .external/ is cloned."
elif command -v bazel >/dev/null 2>&1; then
  bver="$(bazel --version 2>/dev/null || true)"
  warn "bazel on PATH (${bver:-version unknown}) but not bazelisk. ResilientDB pins Bazel 6.0.0 via its own .bazelversion; if this isn't 6.0.0, prefer installing bazelisk (github.com/bazelbuild/bazelisk) so the pin is honored automatically."
else
  fail "Neither bazel nor bazelisk found on PATH. Install bazelisk (recommended) or Bazel 6.0.0 directly."
fi

say "OpenSSL headers"
ssl_header=""
# brew --prefix prints a path whether or not the formula is installed, so the
# -f test below is what actually decides; this only widens the search.
brew_ssl="$(brew --prefix openssl@3 2>/dev/null || true)"
for d in /usr/include /usr/local/include /opt/homebrew/include \
         /opt/homebrew/opt/openssl@3/include /usr/local/opt/openssl@3/include \
         ${brew_ssl:+"$brew_ssl/include"}; do
  [[ -f "$d/openssl/ssl.h" ]] && ssl_header="$d/openssl/ssl.h" && break
done
if [[ -n "$ssl_header" ]]; then
  ok "openssl/ssl.h found at ${ssl_header}"
elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists openssl 2>/dev/null; then
  ok "openssl headers found via pkg-config ($(pkg-config --modversion openssl))"
else
  fail "openssl/ssl.h not found. Install headers: 'brew install openssl' (macOS) or 'apt install libssl-dev' (Debian/Ubuntu)."
fi

say "git / make"
for bin in git make; do
  if command -v "$bin" >/dev/null 2>&1; then
    ok "$bin found"
  else
    fail "$bin not found on PATH."
  fi
done

if [[ "$FAILED" -eq 1 ]]; then
  say "Prerequisite check FAILED — fix the items above before running setup.sh."
  exit 1
elif [[ "$WARNED" -eq 1 ]]; then
  say "Prerequisite check passed with warnings — see above."
  exit 0
else
  say "All prerequisites present."
  exit 0
fi
