#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"

set -a
source .env
set +a

state_dir="$(mktemp -d /tmp/lancedb-session.XXXXXX)"
table_name="cache-create-$(date +%s)"

./build/session-cache-worker \
  --role observer \
  --uri "$LANCEDB_TEST_URI" \
  --state-dir "$state_dir" \
  --table "$table_name" &
observer_pid=$!

./build/session-cache-worker \
  --role writer \
  --scenario create \
  --uri "$LANCEDB_TEST_URI" \
  --state-dir "$state_dir" \
  --table "$table_name"

wait "$observer_pid"