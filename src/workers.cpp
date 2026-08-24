#include "workers.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "lancedb_client.hpp"
#include "table_setup.hpp"

namespace session_mocker {
namespace {

constexpr auto kPollInterval = std::chrono::milliseconds(100);
constexpr int kMaxPollAttempts = 300;  // 30 seconds
constexpr size_t kAppendBatchCount = 2;
constexpr size_t kVectorsPerBatch = 10;
constexpr int kDeleteFirstId = 0;
constexpr int kDeleteLastId = 4;
constexpr size_t kIndexedSeedVectorCount = 256;
constexpr int kNearestIdToDelete = 0;

fs::path observer_ready_path(const Args& args) {
  return fs::path(args.state_dir) / "observer-ready";
}

fs::path writer_done_path(const Args& args) {
  return fs::path(args.state_dir) / "writer-done";
}

void write_signal(const fs::path& path) {
  std::ofstream signal(path);
  if (!signal) {
    throw std::runtime_error("could not write signal: " + path.string());
  }

  signal << "ready\n";
}

void wait_for_signal(const fs::path& path) {
  for (int attempt = 0; attempt < kMaxPollAttempts; ++attempt) {
    if (fs::exists(path)) {
      return;
    }

    std::this_thread::sleep_for(kPollInterval);
  }

  throw std::runtime_error("timed out waiting for signal: " + path.string());
}

void print_row_count(const std::string& label, unsigned long long count) {
  std::cout << label << ": " << count << '\n';
}

void print_tables(const std::string& label, const std::vector<std::string>& tables) {
  std::cout << label << ": " << tables.size() << '\n';

  for (const std::string& table : tables) {
    std::cout << "- " << table << '\n';
  }
}

void print_cache_stats(const std::string& label, const CacheStats& stats) {
  std::cout << label << '\n';

  std::cout << "  index: hits=" << stats.index.hits << " misses=" << stats.index.misses
            << " entries=" << stats.index.num_entries << " bytes=" << stats.index.size_bytes << '\n';

  std::cout << "  metadata: hits=" << stats.metadata.hits << " misses=" << stats.metadata.misses
            << " entries=" << stats.metadata.num_entries << " bytes=" << stats.metadata.size_bytes << '\n';
}

void run_row_count_observer(const Args& args) {
  const Client client = connect_client(args.uri);

  fs::create_directories(args.state_dir);
  fs::remove(observer_ready_path(args));
  fs::remove(writer_done_path(args));

  const unsigned long long initial_count = table_row_count(client, args.table);
  const CacheStats initial_stats = cache_stats(client);

  print_row_count("observer: initial row count", initial_count);
  print_cache_stats("observer: after initial row count", initial_stats);

  write_signal(observer_ready_path(args));
  wait_for_signal(writer_done_path(args));

  const unsigned long long final_count = table_row_count(client, args.table);
  const CacheStats final_stats = cache_stats(client);

  print_row_count("observer: final row count", final_count);
  print_cache_stats("observer: after final row count", final_stats);
}

void run_nearest_id_observer(const Args& args) {
  const Client client = connect_client(args.uri);

  fs::create_directories(args.state_dir);
  fs::remove(observer_ready_path(args));
  fs::remove(writer_done_path(args));

  const int initial_id = nearest_vector_id(client, args.table);
  const CacheStats initial_stats = cache_stats(client);

  std::cout << "observer: initial nearest id: " << initial_id << '\n';
  print_cache_stats("observer: after initial nearest query", initial_stats);

  write_signal(observer_ready_path(args));
  wait_for_signal(writer_done_path(args));

  const int final_id = nearest_vector_id(client, args.table);
  const CacheStats final_stats = cache_stats(client);

  std::cout << "observer: final nearest id: " << final_id << '\n';
  print_cache_stats("observer: after final nearest query", final_stats);
}

void run_setup(const Args& args) {
  const Client client = connect_client(args.uri);

  if (args.scenario == "empty") {
    ensure_table_exists(client, args.table);
    std::cout << "setup: created empty " << args.table << '\n';
    return;
  }

  if (args.scenario == "seed") {
    ensure_table_exists(client, args.table);
    append_random_vector_batches(client, args.table, 1, kVectorsPerBatch);
    std::cout << "setup: seeded " << args.table << " with " << kVectorsPerBatch << " vectors\n";
    return;
  }

  if (args.scenario == "indexed-seed") {
    ensure_table_exists(client, args.table);
    seed_linear_vectors(client, args.table, kIndexedSeedVectorCount);
    create_ivf_flat_index(client, args.table);
    std::cout << "setup: seeded and indexed " << args.table << '\n';
    return;
  }

  throw std::runtime_error("setup scenario must be empty, seed, or indexed-seed");
}

void run_observer(const Args& args) {
  if (args.scenario.empty() || args.scenario == "table-list") {
    const Client client = connect_client(args.uri);
    fs::create_directories(args.state_dir);
    fs::remove(observer_ready_path(args));
    fs::remove(writer_done_path(args));

    const std::vector<std::string> initial_tables = table_names(client);
    const CacheStats initial_stats = cache_stats(client);

    print_tables("observer: initial table list", initial_tables);
    print_cache_stats("observer: after initial table list", initial_stats);

    write_signal(observer_ready_path(args));
    wait_for_signal(writer_done_path(args));

    const std::vector<std::string> final_tables = table_names(client);
    const CacheStats final_stats = cache_stats(client);

    print_tables("observer: final table list", final_tables);
    print_cache_stats("observer: after final table list", final_stats);
    return;
  }

  if (args.scenario == "row-count") {
    run_row_count_observer(args);
    return;
  }

  if (args.scenario == "nearest-id") {
    run_nearest_id_observer(args);
    return;
  }

  throw std::runtime_error("observer scenario must be table-list, row-count, or nearest-id");
}

void run_writer(const Args& args) {
  wait_for_signal(observer_ready_path(args));

  const Client client = connect_client(args.uri);

  if (args.scenario == "create") {
    ensure_table_exists(client, args.table);
    std::cout << "writer: created " << args.table << '\n';
  } else if (args.scenario == "drop") {
    drop_table(client, args.table);
    std::cout << "writer: dropped " << args.table << '\n';
  } else if (args.scenario == "append") {
    append_random_vector_batches(client, args.table, kAppendBatchCount, kVectorsPerBatch);
    std::cout << "writer: appended " << kAppendBatchCount * kVectorsPerBatch << " vectors to " << args.table
              << '\n';
  } else if (args.scenario == "delete-rows") {
    delete_vectors_by_id_range(client, args.table, kDeleteFirstId, kDeleteLastId);
    std::cout << "writer: deleted ids " << kDeleteFirstId << " through " << kDeleteLastId << " from "
              << args.table << '\n';
  } else if (args.scenario == "delete-nearest") {
    delete_vectors_by_id_range(client, args.table, kNearestIdToDelete, kNearestIdToDelete);
    std::cout << "writer: deleted nearest id " << kNearestIdToDelete << " from " << args.table << '\n';
  } else {
    throw std::runtime_error("writer scenario must be create, drop, append, delete-rows, or delete-nearest");
  }

  write_signal(writer_done_path(args));
}

}  // namespace

int run_worker(const Args& args) {
  if (args.uri.empty()) {
    throw std::runtime_error("--uri is required");
  }

  if (args.table.empty()) {
    throw std::runtime_error("--table is required");
  }

  if (args.role == "setup") {
    run_setup(args);
    return 0;
  }

  if (args.state_dir.empty()) {
    throw std::runtime_error("--state-dir is required");
  }

  if (args.role == "observer") {
    run_observer(args);
    return 0;
  }

  if (args.role == "writer") {
    run_writer(args);
    return 0;
  }

  throw std::runtime_error("--role must be observer or writer");
}

}  // namespace session_mocker
