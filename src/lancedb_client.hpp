#pragma once

// Own the LanceDB-C session and connection lifecycle here.
// No process coordination or scenario assertions belong in this module.

#include <lancedb.h>

#include <string>
#include <vector>

#include "app.hpp"

namespace session_mocker {

struct Client {
  LanceDBSession* session = nullptr;
  LanceDBConnection* connection = nullptr;

  ~Client();
  Client() = default;

  Client(const Client&) = delete;             // no-copy constructor
  Client& operator=(const Client&) = delete;  // no-copy assign

  Client(Client&& other) noexcept;
  Client& operator=(Client&& other) noexcept;
};

Client connect_client(const std::string& uri);
std::vector<std::string> table_names(const Client& client);

struct CacheStats {
  LanceDBSessionCacheStats index{};
  LanceDBSessionCacheStats metadata{};
};

CacheStats cache_stats(const Client& client);

}  // namespace session_mocker
