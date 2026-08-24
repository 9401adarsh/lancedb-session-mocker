#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <existing-table-name>" >&2
  exit 1
fi

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"

set -a
source .env
set +a

table_name="$1"
state_dir="$(mktemp -d /tmp/lancedb-session.XXXXXX)"
observer_pid=""

cleanup() {
  if [[ -n "$observer_pid" ]] && kill -0 "$observer_pid" 2>/dev/null; then
    kill "$observer_pid" 2>/dev/null || true
  fi

  rm -rf -- "$state_dir"
}

trap cleanup EXIT

./build/session-cache-worker \
  --role observer \
  --uri "$LANCEDB_TEST_URI" \
  --state-dir "$state_dir" \
  --table "$table_name" &
observer_pid=$!

./build/session-cache-worker \
  --role writer \
  --scenario drop \
  --uri "$LANCEDB_TEST_URI" \
  --state-dir "$state_dir" \
  --table "$table_name"

wait "$observer_pid"