#include "lancedb_client.hpp"

#include <stdexcept>

namespace session_mocker {

Client::~Client() {
  if (connection != nullptr) {
    lancedb_connection_free(connection);
  }
  if (session != nullptr) {
    lancedb_session_free(session);
  }
}

Client::Client(Client&& other) noexcept : session(other.session), connection(other.connection) {
  other.session = nullptr;
  other.connection = nullptr;
}

Client& Client::operator=(Client&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  if (connection != nullptr) {
    lancedb_connection_free(connection);
  }

  if (session != nullptr) {
    lancedb_session_free(session);
  }

  session = other.session;
  connection = other.connection;

  other.session = nullptr;
  other.connection = nullptr;

  return *this;
}

Client connect_client(const std::string& uri) {
  LanceDBSessionOptions options{};
  options.index_cache_bytes = 64ULL * 1024 * 1024;
  options.metadata_cache_bytes = 64ULL * 1024 * 1024;

  Client client;
  client.session = lancedb_session_new(&options);

  if (client.session == nullptr) {
    throw std::runtime_error("failed to create LanceDBSession");
  }

  LanceDBConnectBuilder* builder = lancedb_connect(uri.c_str());

  if (builder == nullptr) {
    throw std::runtime_error("failed to create LanceDBConnectBuilder");
  }

  auto add_storage_option = [&builder](const char* key, const std::string& value) {
    if (value.empty())
      return;
    builder = lancedb_connect_builder_storage_option(builder, key, value.c_str());
    if (builder == nullptr) {
      throw std::runtime_error(std::string("failed to set storage option: ") + key);
    }
  };

  add_storage_option("endpoint", env("AWS_ENDPOINT"));
  add_storage_option("aws_region", env("AWS_DEFAULT_REGION"));
  add_storage_option("aws_access_key_id", env("AWS_ACCESS_KEY_ID"));
  add_storage_option("aws_secret_access_key", env("AWS_SECRET_ACCESS_KEY"));
  add_storage_option("allow_http", env("AWS_ALLOW_HTTP"));
  add_storage_option("aws_s3_addressing_style", "path");

  builder = lancedb_connect_builder_session(builder, client.session);

  if (builder == nullptr) {
    throw std::runtime_error("failed to configure lancedb connection");
  }

  char* error_message = nullptr;

  const LanceDBError result = lancedb_connect_builder_execute(builder, &client.connection, &error_message);

  if (result != LANCEDB_SUCCESS) {
    std::string message = error_message != nullptr ? error_message : lancedb_error_to_message(result);

    if (error_message != nullptr) {
      lancedb_free_string(error_message);
    }

    throw std::runtime_error("failed to connect: " + message);
  }

  return client;
}

std::vector<std::string> table_names(const Client& client) {
  if (client.connection == nullptr) {
    throw std::runtime_error("cannot list tables without a connection");
  }

  char** raw_names = nullptr;
  size_t count = 0;
  char* error_message = nullptr;

  const LanceDBError result =
      lancedb_connection_table_names(client.connection, &raw_names, &count, &error_message);

  if (result != LANCEDB_SUCCESS) {
    const std::string message = error_message != nullptr ? error_message : lancedb_error_to_message(result);
    if (error_message != nullptr) {
      lancedb_free_string(error_message);
    }
    throw std::runtime_error("failed to list tables: " + message);
  }

  std::vector<std::string> names;

  for (size_t index = 0; index < count; index++) {
    names.emplace_back(raw_names[index]);
  }

  lancedb_free_table_names(raw_names, count);

  return names;
}

CacheStats cache_stats(const Client& client) {
  if (client.session == nullptr) {
    throw std::runtime_error("can't read cache stats w/o a session");
  }

  CacheStats stats;
  char* error_message = nullptr;

  LanceDBError result = lancedb_session_index_cache_stats(client.session, &stats.index, &error_message);

  if (result != LANCEDB_SUCCESS) {
    const std::string message = error_message != nullptr ? error_message : lancedb_error_to_message(result);
    if (error_message != nullptr) {
      lancedb_free_string(error_message);
    }
    throw std::runtime_error("failed to read index cache stats: " + message);
  }

  result = lancedb_session_metadata_cache_stats(client.session, &stats.metadata, &error_message);
  if (result != LANCEDB_SUCCESS) {
    const std::string message = error_message != nullptr ? error_message : lancedb_error_to_message(result);
    if (error_message != nullptr) {
      lancedb_free_string(error_message);
    }
    throw std::runtime_error("failed to read metadata cache stats: " + message);
  }

  return stats;
}

}  // namespace session_mocker
