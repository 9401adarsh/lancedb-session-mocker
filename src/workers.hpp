#pragma once

// Coordinate independent reader, writer, and observer processes.

#include "app.hpp"

namespace session_mocker {
int run_worker(const Args& args);
}  // namespace session_mocker
