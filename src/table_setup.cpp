#include "table_setup.hpp"

#include <arrow/api.h>
#include <arrow/c/bridge.h>

#include <algorithm>
#include <array>
#include <random>
#include <stdexcept>

namespace session_mocker {
namespace {
std::shared_ptr<arrow::Schema> table_schema() {
  return arrow::schema({arrow::field("id", arrow::int32()),
                        arrow::field("vector", arrow::fixed_size_list(arrow::float32(), 4))});
}

bool table_exists(const Client& client, const std::string& table_name) {
  const std::vector<std::string> names = table_names(client);
  return std::find(names.begin(), names.end(), table_name) != names.end();
}
}  // namespace

void create_empty_vector_table(const Client& client, const std::string& table_name) {
  const std::shared_ptr<arrow::Schema> schema = table_schema();

  ArrowSchema c_schema{};
  const arrow::Status export_status = arrow::ExportSchema(*schema, &c_schema);

  if (!export_status.ok()) {
    throw std::runtime_error("failed to export Arrow schema: " + export_status.ToString());
  }

  LanceDBTable* table = nullptr;
  char* error_message = nullptr;

  const LanceDBError result =
      lancedb_table_create(client.connection, table_name.c_str(),
                           reinterpret_cast<FFI_ArrowSchema*>(&c_schema), nullptr, &table, &error_message);

  if (c_schema.release != nullptr) {
    c_schema.release(&c_schema);
  }

  if (result != LANCEDB_SUCCESS) {
    const std::string message = error_message != nullptr ? error_message : lancedb_error_to_message(result);
    if (error_message != nullptr) {
      lancedb_free_string(error_message);
    }
    throw std::runtime_error("failed to create table: " + message);
  }

  lancedb_table_free(table);
}

void ensure_table_exists(const Client& client, const std::string& table_name) {
  if (table_exists(client, table_name)) {
    return;
  }
  create_empty_vector_table(client, table_name);
}

void drop_table(const Client& client, const std::string& table_name) {
  if (client.connection == nullptr) {
    throw std::runtime_error("cannot drop a table without a connection.");
  }

  char* error_message = nullptr;

  const LanceDBError result =
      lancedb_connection_drop_table(client.connection, table_name.c_str(), nullptr, &error_message);

  if (result == LANCEDB_SUCCESS) {
    return;
  }

  const std::string message = error_message != nullptr ? error_message : lancedb_error_to_message(result);

  if (error_message != nullptr) {
    lancedb_free_string(error_message);
  }

  throw std::runtime_error("failed to drop table: " + table_name + ". error message: " + message);
}

void append_random_vector_batches(const Client& client, const std::string& table_name, size_t num_batches,
                                  size_t vectors_per_batch) {
  if (num_batches == 0 || vectors_per_batch == 0) {
    throw std::runtime_error("num_batches and vectors_per_batch must be greater than zero");
  }

  LanceDBTable* table = lancedb_connection_open_table(client.connection, table_name.c_str());

  if (table == nullptr) {
    throw std::runtime_error("failed to open table: " + table_name);
  }

  const unsigned long long first_id = lancedb_table_count_rows(table);
  std::mt19937 generator(std::random_device{}());
  std::uniform_real_distribution<float> random_value(-1.0F, 1.0F);

  for (size_t batch_index = 0; batch_index < num_batches; ++batch_index) {
    arrow::Int32Builder id_builder;
    arrow::FixedSizeListBuilder vector_builder(arrow::default_memory_pool(),
                                               std::make_unique<arrow::FloatBuilder>(), 4);
    auto* vector_values = static_cast<arrow::FloatBuilder*>(vector_builder.value_builder());

    for (size_t vector_index = 0; vector_index < vectors_per_batch; ++vector_index) {
      const int id = static_cast<int>(first_id + batch_index * vectors_per_batch + vector_index);

      if (!id_builder.Append(id).ok() || !vector_builder.Append().ok()) {
        lancedb_table_free(table);
        throw std::runtime_error("failed to build Arrow vector batch");
      }

      for (int dimension = 0; dimension < 4; ++dimension) {
        if (!vector_values->Append(random_value(generator)).ok()) {
          lancedb_table_free(table);
          throw std::runtime_error("failed to append random vector value");
        }
      }
    }

    std::shared_ptr<arrow::Array> id_array;
    std::shared_ptr<arrow::Array> vector_array;

    if (!id_builder.Finish(&id_array).ok() || !vector_builder.Finish(&vector_array).ok()) {
      lancedb_table_free(table);
      throw std::runtime_error("failed to finish Arrow vector batch");
    }

    const std::shared_ptr<arrow::RecordBatch> batch =
        arrow::RecordBatch::Make(table_schema(), vectors_per_batch, {id_array, vector_array});

    ArrowArray c_array{};
    ArrowSchema c_schema{};

    const arrow::Status export_status = arrow::ExportRecordBatch(*batch, &c_array, &c_schema);

    if (!export_status.ok()) {
      lancedb_table_free(table);
      throw std::runtime_error("failed to export Arrow vector batch: " + export_status.ToString());
    }

    LanceDBRecordBatchReader* reader = nullptr;
    char* error_message = nullptr;

    const LanceDBError reader_result = lancedb_record_batch_reader_from_arrow(
        reinterpret_cast<FFI_ArrowArray*>(&c_array), reinterpret_cast<FFI_ArrowSchema*>(&c_schema), &reader,
        &error_message);

    if (reader_result != LANCEDB_SUCCESS) {
      if (c_schema.release != nullptr) {
        c_schema.release(&c_schema);
      }

      if (c_array.release != nullptr) {
        c_array.release(&c_array);
      }

      const std::string message =
          error_message != nullptr ? error_message : lancedb_error_to_message(reader_result);

      if (error_message != nullptr) {
        lancedb_free_string(error_message);
      }

      lancedb_table_free(table);
      throw std::runtime_error("failed to create record reader: " + message);
    }

    error_message = nullptr;

    const LanceDBError add_result = lancedb_table_add(table, reader, &error_message);

    if (c_schema.release != nullptr) {
      c_schema.release(&c_schema);
    }

    if (add_result != LANCEDB_SUCCESS) {
      const std::string message =
          error_message != nullptr ? error_message : lancedb_error_to_message(add_result);

      if (error_message != nullptr) {
        lancedb_free_string(error_message);
      }

      lancedb_table_free(table);
      throw std::runtime_error("failed to add vector batch: " + message);
    }
  }

  lancedb_table_free(table);
}

void delete_vectors_by_id_range(const Client& client, const std::string& table_name, int first_id,
                                int last_id) {
  if (first_id > last_id) {
    throw std::runtime_error("first_id must not greater than last_id!");
  }

  LanceDBTable* table = lancedb_connection_open_table(client.connection, table_name.c_str());

  if (table == nullptr) {
    throw std::runtime_error("failed to open table: " + table_name);
  }

  const std::string predicate = "id >= " + std::to_string(first_id) + " AND id <= " + std::to_string(last_id);

  char* error_message = nullptr;
  const LanceDBError result = lancedb_table_delete(table, predicate.c_str(), &error_message);

  const std::string message = error_message != nullptr ? error_message : lancedb_error_to_message(result);

  if (error_message != nullptr) {
    lancedb_free_string(error_message);
  }

  lancedb_table_free(table);

  if (result != LANCEDB_SUCCESS) {
    throw std::runtime_error("failed to delete vectors: " + message);
  }
}

unsigned long long table_row_count(const Client& client, const std::string& table_name) {
  LanceDBTable* table = lancedb_connection_open_table(client.connection, table_name.c_str());

  if (table == nullptr) {
    throw std::runtime_error("failed to open table: " + table_name);
  }

  const unsigned long long count = lancedb_table_count_rows(table);
  lancedb_table_free(table);
  return count;
}

void seed_linear_vectors(const Client& client, const std::string& table_name, size_t vector_count) {
  if (vector_count == 0) {
    throw std::runtime_error("vector_count must be greater than zero");
  }

  LanceDBTable* table = lancedb_connection_open_table(client.connection, table_name.c_str());

  if (table == nullptr) {
    throw std::runtime_error("failed to open table: " + table_name);
  }

  arrow::Int32Builder id_builder;
  arrow::FixedSizeListBuilder vector_builder(arrow::default_memory_pool(),
                                             std::make_unique<arrow::FloatBuilder>(), 4);
  auto* vector_values = static_cast<arrow::FloatBuilder*>(vector_builder.value_builder());

  for (size_t index = 0; index < vector_count; ++index) {
    const int id = static_cast<int>(index);

    if (!id_builder.Append(id).ok() || !vector_builder.Append().ok()) {
      lancedb_table_free(table);
      throw std::runtime_error("failed to build deterministic vector batch");
    }

    if (!vector_values->Append(static_cast<float>(id)).ok() || !vector_values->Append(0.0F).ok() ||
        !vector_values->Append(0.0F).ok() || !vector_values->Append(0.0F).ok()) {
      lancedb_table_free(table);
      throw std::runtime_error("failed to append deterministic vector");
    }
  }

  std::shared_ptr<arrow::Array> id_array;
  std::shared_ptr<arrow::Array> vector_array;

  if (!id_builder.Finish(&id_array).ok() || !vector_builder.Finish(&vector_array).ok()) {
    lancedb_table_free(table);
    throw std::runtime_error("failed to finish deterministic vector batch");
  }

  const std::shared_ptr<arrow::RecordBatch> batch =
      arrow::RecordBatch::Make(table_schema(), vector_count, {id_array, vector_array});

  ArrowArray c_array{};
  ArrowSchema c_schema{};
  const arrow::Status export_status = arrow::ExportRecordBatch(*batch, &c_array, &c_schema);

  if (!export_status.ok()) {
    lancedb_table_free(table);
    throw std::runtime_error("failed to export deterministic vector batch: " + export_status.ToString());
  }

  LanceDBRecordBatchReader* reader = nullptr;
  char* error_message = nullptr;
  const LanceDBError reader_result = lancedb_record_batch_reader_from_arrow(
      reinterpret_cast<FFI_ArrowArray*>(&c_array), reinterpret_cast<FFI_ArrowSchema*>(&c_schema), &reader,
      &error_message);

  if (reader_result != LANCEDB_SUCCESS) {
    if (c_schema.release != nullptr) {
      c_schema.release(&c_schema);
    }

    if (c_array.release != nullptr) {
      c_array.release(&c_array);
    }

    const std::string message =
        error_message != nullptr ? error_message : lancedb_error_to_message(reader_result);

    if (error_message != nullptr) {
      lancedb_free_string(error_message);
    }

    lancedb_table_free(table);
    throw std::runtime_error("failed to create deterministic vector reader: " + message);
  }

  error_message = nullptr;
  const LanceDBError add_result = lancedb_table_add(table, reader, &error_message);

  if (c_schema.release != nullptr) {
    c_schema.release(&c_schema);
  }

  if (add_result != LANCEDB_SUCCESS) {
    const std::string message =
        error_message != nullptr ? error_message : lancedb_error_to_message(add_result);

    if (error_message != nullptr) {
      lancedb_free_string(error_message);
    }

    lancedb_table_free(table);
    throw std::runtime_error("failed to seed deterministic vectors: " + message);
  }

  lancedb_table_free(table);
}

void create_ivf_flat_index(const Client& client, const std::string& table_name) {
  LanceDBTable* table = lancedb_connection_open_table(client.connection, table_name.c_str());

  if (table == nullptr) {
    throw std::runtime_error("failed to open table: " + table_name);
  }

  const char* columns[] = {"vector"};
  LanceDBVectorIndexConfig config{};
  config.num_partitions = 4;
  config.num_sub_vectors = -1;
  config.max_iterations = -1;
  config.sample_rate = 0.0F;
  config.distance_type = LANCEDB_DISTANCE_L2;
  config.replace = 0;

  char* error_message = nullptr;
  const LanceDBError result =
      lancedb_table_create_vector_index(table, columns, 1, LANCEDB_INDEX_IVF_FLAT, &config, &error_message);

  const std::string message = error_message != nullptr ? error_message : lancedb_error_to_message(result);

  if (error_message != nullptr) {
    lancedb_free_string(error_message);
  }

  lancedb_table_free(table);

  if (result != LANCEDB_SUCCESS) {
    throw std::runtime_error("failed to create vector index: " + message);
  }
}

int nearest_vector_id(const Client& client, const std::string& table_name) {
  constexpr std::array<float, 4> query_vector = {0.0F, 0.0F, 0.0F, 0.0F};

  LanceDBTable* table = lancedb_connection_open_table(client.connection, table_name.c_str());

  if (table == nullptr) {
    throw std::runtime_error("failed to open table: " + table_name);
  }

  FFI_ArrowArray** result_arrays = nullptr;
  FFI_ArrowSchema* result_schema = nullptr;
  size_t batch_count = 0;
  char* error_message = nullptr;

  const LanceDBError result =
      lancedb_table_nearest_to(table, query_vector.data(), query_vector.size(), 1, "vector", &result_arrays,
                               &result_schema, &batch_count, &error_message);

  lancedb_table_free(table);

  if (result != LANCEDB_SUCCESS) {
    const std::string message = error_message != nullptr ? error_message : lancedb_error_to_message(result);

    if (error_message != nullptr) {
      lancedb_free_string(error_message);
    }

    throw std::runtime_error("nearest-vector query failed: " + message);
  }

  if (batch_count == 0) {
    lancedb_free_arrow_arrays(result_arrays, batch_count);
    lancedb_free_arrow_schema(result_schema);
    throw std::runtime_error("nearest-vector query returned no results");
  }

  int nearest_id = -1;

  {
    const auto schema = arrow::ImportSchema(reinterpret_cast<ArrowSchema*>(result_schema));

    if (!schema.ok()) {
      lancedb_free_arrow_arrays(result_arrays, batch_count);
      lancedb_free_arrow_schema(result_schema);
      throw std::runtime_error("failed to import query schema: " + schema.status().ToString());
    }

    const auto batch =
        arrow::ImportRecordBatch(reinterpret_cast<ArrowArray*>(result_arrays[0]), schema.ValueUnsafe());

    if (!batch.ok() || batch.ValueUnsafe()->num_rows() == 0) {
      lancedb_free_arrow_arrays(result_arrays, batch_count);
      lancedb_free_arrow_schema(result_schema);
      throw std::runtime_error("failed to import nearest-vector result");
    }

    const auto ids = std::static_pointer_cast<arrow::Int32Array>(batch.ValueUnsafe()->GetColumnByName("id"));

    if (ids == nullptr || ids->IsNull(0)) {
      lancedb_free_arrow_arrays(result_arrays, batch_count);
      lancedb_free_arrow_schema(result_schema);
      throw std::runtime_error("nearest-vector result has no usable id");
    }

    nearest_id = ids->Value(0);
  }

  lancedb_free_arrow_arrays(result_arrays, batch_count);
  lancedb_free_arrow_schema(result_schema);

  return nearest_id;
}

}  // namespace session_mocker
