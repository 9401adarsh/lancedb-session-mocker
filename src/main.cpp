#include <exception>
#include <iostream>

#include "app.hpp"
#include "workers.hpp"

int main(int argc, char** argv) {
  try {
    const session_mocker::Args args = session_mocker::parse_args(argc, argv);
    return session_mocker::run_worker(args);
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}