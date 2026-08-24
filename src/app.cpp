#include "app.hpp"

#include <cstdlib>
#include <stdexcept>

namespace session_mocker {

Args parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; index += 2) {
    const std::string option = argv[index];
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value for " + option);
    }

    const std::string value = argv[index + 1];

    if (option == "--role") {
      args.role = value;
    } else if (option == "--scenario") {
      args.scenario = value;
    } else if (option == "--uri") {
      args.uri = value;
    } else if (option == "--state-dir") {
      args.state_dir = value;
    } else if (option == "--table") {
      args.table = value;
    } else {
      throw std::runtime_error("unknown option: " + option);
    }
  }

  return args;
}

std::string env(const char* name) {
  const char* value = std::getenv(name);

  if (value == nullptr) {
    return "";
  }

  return value;
}

// TODO: Add file and JSON helpers only when a scenario needs them.

}  // namespace session_mocker
