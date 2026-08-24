cmake_minimum_required(VERSION 3.24)
project(lancedb_session_mocker LANGUAGES CXX)

include(FetchContent)

option(SESSION_MOCKER_FETCH_LANCEDB_C "Fetch the pinned lancedb-c source" ON)
set(
  SESSION_MOCKER_LANCEDB_C_REV
  "daabceaeb24c07e2ada27801e18df375f2a3b01c"
  CACHE STRING "Immutable lancedb-c revision")

if(SESSION_MOCKER_FETCH_LANCEDB_C)
  FetchContent_Declare(
    lancedb_c
    GIT_REPOSITORY https://github.com/lancedb/lancedb-c.git
    GIT_TAG ${SESSION_MOCKER_LANCEDB_C_REV}
    GIT_SHALLOW TRUE)
  FetchContent_GetProperties(lancedb_c)
  if(NOT lancedb_c_POPULATED)
    FetchContent_Populate(lancedb_c)
  endif()
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SESSION_MOCKER_LANCEDB_PROFILE debug)
    set(SESSION_MOCKER_CARGO_PROFILE_ARGS)
  else()
    set(SESSION_MOCKER_LANCEDB_PROFILE release)
    set(SESSION_MOCKER_CARGO_PROFILE_ARGS --release)
  endif()
  set(
    SESSION_MOCKER_LANCEDB_LIBRARY
    "${CMAKE_BINARY_DIR}/lancedb-c-target/${SESSION_MOCKER_LANCEDB_PROFILE}/${CMAKE_SHARED_LIBRARY_PREFIX}lancedb${CMAKE_SHARED_LIBRARY_SUFFIX}")

  add_custom_command(
    OUTPUT "${SESSION_MOCKER_LANCEDB_LIBRARY}"
    COMMAND cargo build ${SESSION_MOCKER_CARGO_PROFILE_ARGS} --target-dir
            "${CMAKE_BINARY_DIR}/lancedb-c-target"
    WORKING_DIRECTORY "${lancedb_c_SOURCE_DIR}"
    COMMENT "Building pinned LanceDB-C library")
  add_custom_target(session-mocker-lancedb-build
    DEPENDS "${SESSION_MOCKER_LANCEDB_LIBRARY}")
else()
  find_package(lancedb CONFIG REQUIRED)
  set(lancedb_c_SOURCE_DIR "" CACHE PATH "Path containing lancedb-c/include")
endif()

find_package(Arrow REQUIRED)
find_package(Threads REQUIRED)

set(
  SESSION_MOCKER_CPP_FILES
  src/main.cpp
  src/app.cpp
  src/app.hpp
  src/lancedb_client.cpp
  src/lancedb_client.hpp
  src/table_setup.cpp
  src/table_setup.hpp
  src/workers.cpp
  src/workers.hpp)

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
  add_custom_target(
    format
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i ${SESSION_MOCKER_CPP_FILES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Formatting C++ source files")
  add_custom_target(
    format-check
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${SESSION_MOCKER_CPP_FILES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking C++ source formatting")
else()
  message(WARNING "clang-format was not found; format targets are unavailable")
endif()

add_executable(
  session-cache-worker
  src/main.cpp
  src/app.cpp
  src/lancedb_client.cpp
  src/table_setup.cpp
  src/workers.cpp)
target_compile_features(session-cache-worker PRIVATE cxx_std_20)
if(SESSION_MOCKER_FETCH_LANCEDB_C)
  add_dependencies(session-cache-worker session-mocker-lancedb-build)
  target_link_libraries(session-cache-worker PRIVATE "${SESSION_MOCKER_LANCEDB_LIBRARY}")
else()
  target_link_libraries(session-cache-worker PRIVATE lancedb)
endif()
target_link_libraries(session-cache-worker PRIVATE Threads::Threads Arrow::arrow_shared)
if(lancedb_c_SOURCE_DIR)
  target_include_directories(session-cache-worker PRIVATE "${lancedb_c_SOURCE_DIR}/include")
endif()
