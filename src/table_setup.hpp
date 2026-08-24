#pragma once

// Create and mutate the test table fixture.
// Keep Arrow batch construction isolated in this module.

#include <cstddef>

#include "lancedb_client.hpp"

namespace session_mocker {

void create_empty_vector_table(const Client& client, const std::string& table_name);
void ensure_table_exists(const Client& client, const std::string& table_name);
void drop_table(const Client& client, const std::string& table_name);
void append_random_vector_batches(const Client& client, const std::string& table_name, size_t num_batches,
                                  size_t vectors_per_batch);
unsigned long long table_row_count(const Client& client, const std::string& table_name);
void delete_vectors_by_id_range(const Client& client, const std::string& table_name, int first_id,
                                int last_id);
void seed_linear_vectors(const Client& client, const std::string& table_name, size_t vector_count);
void create_ivf_flat_index(const Client& client, const std::string& table_name);
int nearest_vector_id(const Client& client, const std::string& table_name);

}  // namespace session_mocker
