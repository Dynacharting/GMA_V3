# ===========================================================================
# krn_data — L1 schema, codec and (later) shim (ENC-1481)
#
# Design: specs/2026-10-14-krn-data-layer/SPEC.md; docs/data-layer.md.
#
# LAYERING, WHICH IS THE ONE THING NOT TO GET WRONG:
#
#     krn_data  ->  krn_engine          (this direction only)
#
# `krn_engine` depends on neither this library nor any connector, and ARROW
# AND THE CLICKHOUSE CLIENT MUST NEVER BECOME ENGINE DEPENDENCIES (SPEC D14,
# docs/data-layer.md §4 rule 6). That is not style. D14's client profile is
# "live-only OR reads a central L1 over the wire with no local store" — two
# variants: the LIVE-ONLY one omits this library entirely, so `krn_server` must
# link and run with KRN_BUILD_DATA=OFF and nothing missing, while the
# over-the-wire one needs Arrow IPC and therefore does ship krn_data. The
# invariant holds for both because it constrains krn_engine, not the product.
#
# The invariant is asserted, not asserted-about: `tools/check_engine_isolation.py`
# runs as the ctest case `krn_engine_isolation` and fails the build if an Arrow
# or ClickHouse include appears in engine code, if a `data/` header becomes
# reachable from the engine's include path, or if krn_data / Arrow appears
# anywhere in krn_engine's or krn_server's TRANSITIVE link closure (the
# umbrella `krn` target is where a one-level check was blind).
#
# The engine-facing PODs are deliberately NOT here — they live in
# `include/krn/data/Record.hpp`, inside krn_engine, because ENC-1486's
# `Dispatcher::onRecord` is engine code and has to name the type. The split is
# explained at the top of that header.
# ===========================================================================

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(KrnDataArrow)

add_library(krn_data STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src/Schema.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/krn/data/Schema.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/DatabentoAdapter.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/krn/data/DatabentoAdapter.hpp"
  # Promoted out of DatabentoAdapterTest.cpp (ENC-1483, begun under the
  # cancelled ENC-1498). The byte-for-byte gate against the five real vendor
  # fixtures still compares THIS code, so the non-circularity argument is
  # unchanged -- see DbnEncoder.hpp.
  "${CMAKE_CURRENT_SOURCE_DIR}/src/DbnEncoder.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/krn/data/DbnEncoder.hpp"
  # ENC-1483: the ingest job.
  # Verdict first, because everything below classifies its refusals through it:
  # a finding (exit 1) is not an inability to run (exit 2).
  "${CMAKE_CURRENT_SOURCE_DIR}/src/Verdict.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/krn/data/Verdict.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/DatabentoCsv.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/krn/data/DatabentoCsv.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ClickHouseHttp.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/krn/data/ClickHouseHttp.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/IngestWindows.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/krn/data/IngestWindows.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/IngestJob.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/krn/data/IngestJob.hpp"
)

# PUBLIC: a consumer of krn_data sees the L1 schema headers.
target_include_directories(krn_data PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include")

# krn_engine, and not `krn`: this library needs the engine's PODs and
# SymbolTable, and must not acquire the market connector by accident.
target_link_libraries(krn_data PUBLIC krn_engine krn::arrow)

krn_apply_warnings(krn_data)

message(STATUS "krn_data: enabled (Arrow: ${KRN_DATA_ARROW_PROVENANCE})")

# ---------------------------------------------------------------------------
# krn_ingest -- the ENC-1483 ingest job's command line.
# ---------------------------------------------------------------------------
# An EXECUTABLE in `data/`, not a ctest case, and that is deliberate. It talks
# to a live ClickHouse, and a test that SKIPS when the server is absent looks
# exactly like a test that passed -- the ENC-1095 failure, where three sessions
# read a green 188/188 as renderer coverage. So the logic that can be wrong
# (the gma.ingest_windows read rule, the CSV parse, the window arithmetic) is
# unit-tested in krn_data_tests with no server at all, and the live exercise is
# this binary, whose exit code is the verdict: 0 clean, 1 a finding, 2 COULD
# NOT RUN. A missing server exits 2 and cannot be mistaken for a pass.
#
# It is not registered with ctest for the same reason.
add_executable(krn_ingest "${CMAKE_CURRENT_SOURCE_DIR}/tools/krn_ingest_main.cpp")
target_link_libraries(krn_ingest PRIVATE krn_data)
krn_apply_warnings(krn_ingest)

# ---------------------------------------------------------------------------
# Tests — a SEPARATE binary from krn_tests, on purpose.
# ---------------------------------------------------------------------------
# Linking krn_data into `krn_tests` would put Arrow in the same executable as
# every engine test, so a stray `#include <arrow/...>` in engine test code
# would compile. Keeping the binaries apart means the engine test suite is
# still built against an Arrow-free include path, which is the thing being
# protected.
if (KRN_BUILD_TESTS AND TARGET GTest::gtest_main)
  file(GLOB KRN_DATA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  add_executable(krn_data_tests ${KRN_DATA_TEST_SOURCES})
  target_link_libraries(krn_data_tests PRIVATE krn_data GTest::gtest_main)
  # The DBN fixtures are REAL VENDOR BYTES (data/tests/fixtures/dbn/, see
  # PROVENANCE.md there) and are read from the source tree rather than copied
  # beside the binary: a copy step is one more thing that can silently copy
  # nothing and leave a test skipping instead of failing. The path is absolute
  # so it holds regardless of the test's working directory, and the fixture
  # test FAILS (never skips) when a file is missing.
  target_compile_definitions(krn_data_tests PRIVATE
    KRN_DATA_DBN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/dbn")
  krn_apply_warnings(krn_data_tests)
  add_test(NAME krn_data_tests
    COMMAND krn_data_tests
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:krn_data_tests>")
endif()
