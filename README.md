# LanceDB-C session-cache scenarios

This project checks whether an existing LanceDB-C session observes changes made
by a separate LanceDB-C client against the same S3-compatible database.

Each worker process creates its own `LanceDBSession` and connection. The
observer performs an operation, waits while the writer changes the database,
then repeats that operation using its original session. A temporary local
`state_dir` exchanges `observer-ready` and `writer-done` marker files so the
ordering is deterministic. It does not store LanceDB data.

## Scenarios

| Script | Observer operation | Writer mutation | Expected result |
| --- | --- | --- | --- |
| `run_create_scenario.sh` | List tables | Create a table | Final list contains the new table |
| `run_drop_scenario.sh <table>` | List tables | Drop a table | Final list omits the table |
| `run_vector_append_scenario.sh` | Count rows | Append 20 random vectors | `0 -> 20` rows |
| `run_vector_delete_scenario.sh` | Count rows | Delete IDs `0` through `4` | `10 -> 5` rows |
| `run_indexed_query_delete_scenario.sh` | Find nearest vector to `[0, 0, 0, 0]` | Delete ID `0` | Nearest ID changes `0 -> 1` |

The final scenario seeds 256 deterministic vectors and creates an IVF_FLAT
index. It exercises the reported index-cache counters as well as query
freshness.

## S3 / Ceph RGW setup

Create the S3 bucket first, then copy `.env.example` to `.env` and fill in
your own credentials and endpoint. Do not commit `.env`.

```sh
cp .env.example .env
```

For a local Ceph development cluster started with `vstart.sh`, use its RGW
endpoint and the S3 credentials printed by `vstart.sh`. Path-style addressing
is normally required for this setup.

## Build

The default configuration fetches the pinned LanceDB-C revision
`daabceaeb24c07e2ada27801e18df375f2a3b01c`. Apache Arrow C++ must be
discoverable by CMake.

```sh
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel
```

To use an already-built LanceDB-C library instead, configure with:

```sh
cmake -S . -B build \
  -DSESSION_MOCKER_FETCH_LANCEDB_C=OFF \
  -Dlancedb_c_SOURCE_DIR=/path/to/lancedb-c
```

## Run

Each scenario sources `.env` and creates its own temporary coordination
directory. Run any scenario from the repository root:

```sh
./scripts/run_create_scenario.sh
./scripts/run_vector_append_scenario.sh
./scripts/run_vector_delete_scenario.sh
./scripts/run_indexed_query_delete_scenario.sh
```

For the drop scenario, first provide an existing table name, for example one
printed by the create scenario:

```sh
./scripts/run_drop_scenario.sh cache-create-1234567890
```

## Observed result

On the configured Ceph RGW/S3 backend, no staleness was observed in the five
scenarios above. In particular, the indexed query returned ID `0` before a
separate writer deleted that row, then returned ID `1` afterwards while the
observer retained its original session. The observer's index cache had active
hits and entries during that scenario.

This is evidence for the tested LanceDB-C revision, backend, and operation
sequence; it is not a guarantee for every version, backend configuration, or
concurrent workload.

## Formatting

```sh
cmake --build build --target format
cmake --build build --target format-check
```

`format` rewrites C++ files. `format-check` reports formatting problems without
modifying files.
