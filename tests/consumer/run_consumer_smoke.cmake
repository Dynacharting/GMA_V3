# Consumer smoke tests (ENC-1553, purity SPEC D5). Run by ctest as
#   cmake -DMODE=fetchcontent|install -DKRN_SOURCE_DIR=... -DKRN_BINARY_DIR=...
#         [-DKRN_SMOKE_URL=file://...] [-DKRN_SMOKE_REF=<commit>] -P this-file
# Each run starts from an empty directory under KRN_BINARY_DIR/consumer_smoke.
cmake_minimum_required(VERSION 3.20)

foreach (_v IN ITEMS MODE KRN_SOURCE_DIR KRN_BINARY_DIR KRN_CXX_COMPILER KRN_GENERATOR)
  if (NOT DEFINED ${_v} OR "${${_v}}" STREQUAL "")
    message(FATAL_ERROR "KRN_SMOKE: -D${_v} is required")
  endif()
endforeach()
if (NOT KRN_CONFIG)
  set(KRN_CONFIG Debug)
endif()
set(_root "${KRN_BINARY_DIR}/consumer_smoke/${MODE}")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_root}")
cmake_host_system_information(RESULT _jobs QUERY NUMBER_OF_LOGICAL_CORES)

function(_run what)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE _rc)
  if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "KRN_SMOKE FAIL: ${what} exited ${_rc}")
  endif()
endfunction()

set(_common -G "${KRN_GENERATOR}" "-DCMAKE_BUILD_TYPE=${KRN_CONFIG}"
  "-DCMAKE_CXX_COMPILER=${KRN_CXX_COMPILER}")
if (KRN_RAPIDJSON_ROOT)
  list(APPEND _common "-DRAPIDJSON_ROOT=${KRN_RAPIDJSON_ROOT}")
endif()

if (MODE STREQUAL "fetchcontent")
  # FetchContent sees only commits, so this checks the committed HEAD.
  if (NOT KRN_SMOKE_URL)
    set(KRN_SMOKE_URL "file://${KRN_SOURCE_DIR}")
  endif()
  if (NOT KRN_SMOKE_REF)
    set(KRN_SMOKE_REF HEAD)
  endif()
  string(REGEX REPLACE "^file://" "" _repo "${KRN_SMOKE_URL}")
  execute_process(COMMAND git -C "${_repo}" rev-parse --verify "${KRN_SMOKE_REF}^{commit}"
    OUTPUT_VARIABLE _tag OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _rc)
  if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "KRN_SMOKE FAIL: cannot resolve ${KRN_SMOKE_REF} in ${_repo} "
      "(the fetchcontent smoke needs a git checkout)")
  endif()
  if (KRN_SMOKE_REF STREQUAL "HEAD")
    execute_process(COMMAND git -C "${KRN_SOURCE_DIR}" diff --quiet HEAD --
        CMakeLists.txt cmake RESULT_VARIABLE _dirty)
    if (NOT _dirty EQUAL 0)
      message(WARNING "KRN_SMOKE: uncommitted CMake edits are NOT what this checks")
    endif()
  endif()
  message(STATUS "KRN_SMOKE: fetching ${KRN_SMOKE_URL} at ${_tag}")
  set(_expected "krn_engine,krn_connector_synthetic")  # comma-separated
  _run("consumer configure" "${CMAKE_COMMAND}"
    -S "${KRN_SOURCE_DIR}/tests/consumer/fetchcontent" -B "${_root}/build" ${_common}
    "-DKRN_SMOKE_URL=${KRN_SMOKE_URL}" "-DKRN_SMOKE_TAG=${_tag}"
    "-DKRN_SMOKE_CHECKOUT=${KRN_SOURCE_DIR}" "-DKRN_SMOKE_EXPECTED_TARGETS=${_expected}"
    -DKRN_BUILD_TESTS=ON -DKRN_BUILD_DATA=ON -DKRN_BUILD_BENCHMARKS=ON)
  _run("consumer build" "${CMAKE_COMMAND}" --build "${_root}/build"
    --target krn_consumer_probe --parallel ${_jobs})
  _run("consumer probe" "${_root}/build/krn_consumer_probe")
elseif (MODE STREQUAL "install")
  set(_prefix "${_root}/prefix")
  _run("cmake --install" "${CMAKE_COMMAND}" --install "${KRN_BINARY_DIR}"
    --prefix "${_prefix}" --config "${KRN_CONFIG}")
  _run("consumer configure" "${CMAKE_COMMAND}"
    -S "${KRN_SOURCE_DIR}/tests/consumer/find_package" -B "${_root}/build" ${_common}
    "-DCMAKE_PREFIX_PATH=${_prefix}" -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
    "-DKRN_SMOKE_PREFIX=${_prefix}" "-DKRN_SMOKE_SOURCE_DIR=${KRN_SOURCE_DIR}"
    "-DKRN_SMOKE_BINARY_DIR=${KRN_BINARY_DIR}")
  _run("consumer build" "${CMAKE_COMMAND}" --build "${_root}/build" --parallel ${_jobs})
  _run("consumer probe" "${_root}/build/krn_consumer_probe")
else()
  message(FATAL_ERROR "KRN_SMOKE: unknown MODE '${MODE}'")
endif()
message(STATUS "KRN_SMOKE: ${MODE} passed")
