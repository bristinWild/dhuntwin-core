#!/usr/bin/env bash
# Re-extract the reference data and re-record the golden outputs from the
# ORIGINAL TypeScript engine (tuhin-codes/DhunTwin-OS) at a pinned commit.
#   scripts/regen-golden.sh [commit]     (default: the commit this port tracks)
# Needs git and Node 20+ (tsx is fetched with npx).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
commit="${1:-b6afcb2}"
upstream="${DHUN_UPSTREAM:-$here/build/upstream}"

if [[ ! -d "$upstream/.git" ]]; then
  git clone https://github.com/tuhin-codes/DhunTwin-OS "$upstream"
fi
git -C "$upstream" fetch --quiet origin
git -C "$upstream" checkout --quiet "$commit"

export DHUN_UPSTREAM="$upstream"
npx --yes tsx@4 "$here/scripts/extract-data.ts"
TZ=Asia/Kolkata npx --yes tsx@4 "$here/scripts/golden.ts"
echo "Data and goldens regenerated from DhunTwin-OS@$commit. Run scripts/test-engine.sh."
