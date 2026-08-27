#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"

set -a
source .env
set +a

state_dir="$(mktemp -d /tmp/lancedb-session.XXXXXX)"
table_name="indexed-query-$(uuidgen | tr -d '-')"
observer_pid=""
worker="${SESSION_MOCKER_WORKER:-./build/session-cache-worker}"

cleanup() {
  if [[ -n "$observer_pid" ]] && kill -0 "$observer_pid" 2>/dev/null; then
    kill "$observer_pid" 2>/dev/null || true
  fi

  rm -rf -- "$state_dir"
}

trap cleanup EXIT

"$worker" \
  --role setup \
  --scenario indexed-seed \
  --uri "$LANCEDB_TEST_URI" \
  --table "$table_name"

"$worker" \
  --role observer \
  --scenario nearest-id \
  --uri "$LANCEDB_TEST_URI" \
  --state-dir "$state_dir" \
  --table "$table_name" &
observer_pid=$!

"$worker" \
  --role writer \
  --scenario delete-nearest \
  --uri "$LANCEDB_TEST_URI" \
  --state-dir "$state_dir" \
  --table "$table_name"

wait "$observer_pid"
