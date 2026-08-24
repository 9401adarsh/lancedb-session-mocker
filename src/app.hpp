#pragma once

// Shared, dependency-light types and helpers.
// Write these first: every other module depends on this contract.

#include <filesystem>
#include <string>

namespace session_mocker {

namespace fs = std::filesystem;

struct Args {
  std::string role;
  std::string scenario;
  std::string uri;
  std::string state_dir;
  std::string table;
};

Args parse_args(int argc, char** argv);
std::string env(const char* name);

}  // namespace session_mocker
