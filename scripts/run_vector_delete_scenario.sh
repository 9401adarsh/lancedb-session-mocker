#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"

set -a
source .env
set +a

state_dir="$(mktemp -d /tmp/lancedb-session.XXXXXX)"
table_name="vector-delete-$(date +%s)"
observer_pid=""

cleanup() {
if [[ -n "$observer_pid" ]] && kill -0 "$observer_pid" 2>/dev/null; then
    kill "$observer_pid" 2>/dev/null || true
fi

rm -rf -- "$state_dir"
}

trap cleanup EXIT

./build/session-cache-worker \
--role setup \
--scenario seed \
--uri "$LANCEDB_TEST_URI" \
--table "$table_name"

./build/session-cache-worker \
--role observer \
--scenario row-count \
--uri "$LANCEDB_TEST_URI" \
--state-dir "$state_dir" \
--table "$table_name" &
observer_pid=$!

./build/session-cache-worker \
--role writer \
--scenario delete-rows \
--uri "$LANCEDB_TEST_URI" \
--state-dir "$state_dir" \
--table "$table_name"

wait "$observer_pid"