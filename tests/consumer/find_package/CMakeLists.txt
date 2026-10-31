# Minimal find_package consumer of an installed engine (ENC-1553). Driven by
# tests/consumer/run_consumer_smoke.cmake; not part of the engine's build.
cmake_minimum_required(VERSION 3.20)
project(krn_consumer_find_package LANGUAGES CXX)

find_package(krn CONFIG REQUIRED)

# The package found must be the one just installed, not another prefix's.
file(REAL_PATH "${krn_DIR}" _dir)
file(REAL_PATH "${KRN_SMOKE_PREFIX}" _prefix)
string(FIND "${_dir}" "${_prefix}/" _p)
if (NOT _p EQUAL 0)
  message(FATAL_ERROR "KRN_SMOKE FAIL: krn_DIR ${_dir} is not under ${_prefix}")
endif()
message(STATUS "KRN_SMOKE ok: krn_DIR is ${_dir}")

# No include dir may point back into the producer's source or build tree.
get_target_property(_inc krn::krn_engine INTERFACE_INCLUDE_DIRECTORIES)
# (The prefix itself sits under the producer's build tree, so it is exempt.)
foreach (_d IN LISTS _inc)
  string(FIND "${_d}/" "${_prefix}/" _in_prefix)
  foreach (_bad IN ITEMS "${KRN_SMOKE_SOURCE_DIR}" "${KRN_SMOKE_BINARY_DIR}")
    string(FIND "${_d}" "${_bad}" _q)
    if (_q EQUAL 0 AND NOT _in_prefix EQUAL 0)
      message(FATAL_ERROR "KRN_SMOKE FAIL: include dir ${_d} is in ${_bad}")
    endif()
  endforeach()
endforeach()
if (NOT "${_prefix}/include" IN_LIST _inc)
  message(FATAL_ERROR "KRN_SMOKE FAIL: ${_prefix}/include not in [${_inc}]")
endif()
message(STATUS "KRN_SMOKE ok: krn::krn_engine includes [${_inc}]")

add_executable(krn_consumer_probe src/main.cpp)
target_link_libraries(krn_consumer_probe PRIVATE krn::krn_engine krn::krn_host)
