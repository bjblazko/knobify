#!/usr/bin/env bash
# Local, GitHub-independent verification loop — see
# docs/adr/0003-testing-strategy.md. Runs the same checks
# .github/workflows/ci.yml will run once the repo has a remote, but never
# depends on GitHub to be useful.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== Running native unit tests =="
pio test -e native

echo "== Compiling firmware (esp32-s3, no flashing) =="
pio run -e esp32-s3

echo "== check.sh OK =="
