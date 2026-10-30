cmake_minimum_required(VERSION 3.20)
project(KRN LANGUAGES CXX)

# ---- Options ----
option(KRN_BUILD_TESTS "Build unit tests in tests/" OFF)
option(KRN_BUILD_BENCHMARKS "Build Google Benchmark suite in benchmarks/" OFF)
option(KRN_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

# Data layer (ENC-1481, specs/2026-10-14-krn-data-layer/SPEC.md).
#
# THE DEFAULT FOLLOWS KRN_BUILD_TESTS, and an explicit -DKRN_BUILD_DATA always
# wins. Three constraints had to hold at once and no fixed default holds all
# three:
#
#   * Nothing may be silently skipped in a configuration that runs tests. A
#     skipped target reads exactly like a passing one and this repo has paid for
#     that twice (ENC-994 / ENC-1095). The acceptance command and every gate
#     configuration pass -DKRN_BUILD_TESTS=ON, so krn_data, krn_data_tests and
#     both engine-isolation cases are built and run there.
#   * The documented quickstart — `cmake -B build -DCMAKE_BUILD_TYPE=Release`,
#     tests off — must still work offline. A fixed ON made it fetch a 17 MB
#     Arrow release unconditionally and HARD-FAIL at the download behind a
#     firewall, where before this library existed it fetched nothing at all
#     (googletest sits behind KRN_BUILD_TESTS=OFF too).
#   * KRN_BUILD_DATA=OFF is SPEC D14's live-only CLIENT VARIANT: no local store,
#     no Arrow. `krn_server` must build, link and run identically with it off,
#     which is the whole reason the library is separately linkable. (D14's other
#     client variant reads a central L1 over the wire — that one needs Arrow IPC,
#     i.e. it needs this library. The invariant is about krn_engine, not about
#     the product: see the engine isolation gate at the bottom of this file.)
#
# The cost of ON, measured on this toolchain 2026-10-14 and recorded in
# docs/data-layer.md §15: a cold configure fetches a 17 MB Arrow source release
# (2m33s on this link) and the build adds 45s Debug / 63s Release on 16 cores.
# A warm build dir pays none of it.
# Resolve, then report the value AGAINST the default rather than claiming to
# know the provenance. `option()` writes the cache on the first configure, so
# `if (DEFINED KRN_BUILD_DATA)` is true on every configure after that and
# cannot tell "the user passed -D today" from "we defaulted it last week" —
# a status line that said "explicit or cached" was wrong in the common case,
# and the "why" is the only reason the line exists. Comparing the resolved
# value with the default this KRN_BUILD_TESTS would produce is always true and
# never goes stale.
set(_krn_data_default ${KRN_BUILD_TESTS})
option(KRN_BUILD_DATA
  "Build krn_data: the L1 Arrow schema and codec. Defaults to KRN_BUILD_TESTS. OFF selects SPEC D14's live-only client variant (no Arrow fetched or built)."
  ${_krn_data_default})

if (KRN_BUILD_DATA AND _krn_data_default)
  set(_krn_data_why "matches the default for KRN_BUILD_TESTS=${KRN_BUILD_TESTS}")
elseif (NOT KRN_BUILD_DATA AND NOT _krn_data_default)
  set(_krn_data_why "matches the default for KRN_BUILD_TESTS=${KRN_BUILD_TESTS}")
else()
  set(_krn_data_why
    "OVERRIDDEN — the default for KRN_BUILD_TESTS=${KRN_BUILD_TESTS} is ${_krn_data_default}, and this build dir's cache says otherwise")
endif()

if (KRN_BUILD_DATA)
  message(STATUS
    "KRN_BUILD_DATA=ON [${_krn_data_why}] — krn_data is built; Arrow is "
    "found via find_package(Arrow CONFIG) or fetched and built from source.")
else()
  message(STATUS
    "KRN_BUILD_DATA=OFF [${_krn_data_why}] — SPEC D14's live-only client "
    "variant: NO Arrow is fetched or built, and krn_data / krn_data_tests do not "
    "exist in this build. Pass -DKRN_BUILD_DATA=ON to build them regardless.")
endif()

# A SKIP IN A CONFIGURATION THAT RUNS TESTS MUST NOT BE SILENT. The default
# above makes `KRN_BUILD_TESTS=ON` imply the data layer, but a CACHE ENTRY
# OUTLIVES IT: configure a dir with tests off (data defaults off), then
# re-run with -DKRN_BUILD_TESTS=ON, and `option()` leaves KRN_BUILD_DATA=OFF.
# ctest then goes green over a build with no krn_data_tests and no positive
# control — exactly the ENC-994 / ENC-1095 mode this default was chosen to
# avoid, arriving through the cache instead of through the default. It cannot
# be fixed by forcing the value (that would silently discard a deliberate
# -DKRN_BUILD_DATA=OFF), so it is NAMED instead.
if (KRN_BUILD_TESTS AND NOT KRN_BUILD_DATA)
  message(WARNING
    "KRN_BUILD_DATA=OFF while KRN_BUILD_TESTS=ON: krn_data_tests is NOT "
    "registered with ctest and krn_engine_isolation's second (krn_data) "
    "positive control is absent, so a green `ctest` in this build directory "
    "EXCLUDES the entire data layer. Intended if you asked for SPEC D14's live-only "
    "client variant. If you did not: this is a STALE CACHE — `option()` wrote "
    "KRN_BUILD_DATA on the first configure of this directory and a later "
    "-DKRN_BUILD_TESTS=ON does not move it. Re-run with an explicit "
    "-DKRN_BUILD_DATA=ON, or configure a fresh build directory.")
endif()

# Microarchitecture (ENC-790 / H5): -march=native pins the binary to the exact
# build CPU and SIGILLs on older hosts. Release/RelWithDebInfo therefore default
# to a portable baseline (KRN_ARCH_BASELINE, x86-64-v2 ≈ any 2009+ x86_64 CPU);
# -march=native is opt-in via KRN_NATIVE_ARCH. -O3 and LTO are kept regardless.
option(KRN_NATIVE_ARCH
  "Build Release with -march=native (non-portable: pins the binary to the build CPU; SIGILL on other hosts)"
  OFF)
set(KRN_ARCH_BASELINE "x86-64-v2" CACHE STRING
  "Portable -march baseline used for Release on x86_64 when KRN_NATIVE_ARCH is OFF (empty string disables -march entirely)")

# Sanitizers (ENC-801 / M15): opt-in, default off so a plain `cmake .. && make`
# is byte-for-byte unaffected. When set, the flags are injected into compile AND
# link for every target in this build (engine, connectors, server, tests, gtest).
set(KRN_SANITIZE "off" CACHE STRING
  "Enable a sanitizer for all targets: off|address|thread|undefined|address+undefined")
set_property(CACHE KRN_SANITIZE PROPERTY STRINGS off address thread undefined address+undefined)

# ---- C++ standard ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Platform defs (Beast/Asio on Windows) ----
if (WIN32)
  add_definitions(-D_WIN32_WINNT=0x0A00) # Windows 10
endif()

# ---- Third-party root
# Override with: -DTHIRD_PARTY_ROOT=C:/wherever
if (NOT DEFINED THIRD_PARTY_ROOT)
  if (WIN32)
    set(THIRD_PARTY_ROOT "C:/thirdparty")
  else()
    set(THIRD_PARTY_ROOT "${PROJECT_SOURCE_DIR}/third_party")
  endif()
endif()

# ---- Threads
find_package(Threads REQUIRED)

# ---- OpenSSL (optional — enables wss:// in WsFeedClient)
find_package(OpenSSL QUIET)
if (OpenSSL_FOUND)
  message(STATUS "OpenSSL found: ${OPENSSL_VERSION} — wss:// feed support enabled")
else()
  message(STATUS "OpenSSL not found — wss:// feed support disabled (ws:// still works)")
endif()

# =======================
# Sanitizers (opt-in; ENC-801 / M15)
# =======================
# Injected at directory scope BEFORE any target is defined, so every target
# created below (including FetchContent's googletest/benchmark) is instrumented
# in both compile and link. Default "off" => no flags => default build unchanged.
if (NOT KRN_SANITIZE STREQUAL "off")
  if (MSVC)
    message(WARNING "KRN_SANITIZE=${KRN_SANITIZE} is not supported with this MSVC toolchain — ignoring")
  else()
    if (KRN_SANITIZE STREQUAL "address")
      set(_KRN_SAN_FLAGS -fsanitize=address -fno-omit-frame-pointer)
    elseif (KRN_SANITIZE STREQUAL "thread")
      set(_KRN_SAN_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
    elseif (KRN_SANITIZE STREQUAL "undefined")
      set(_KRN_SAN_FLAGS -fsanitize=undefined -fno-omit-frame-pointer)
    elseif (KRN_SANITIZE STREQUAL "address+undefined")
      set(_KRN_SAN_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
    else()
      message(FATAL_ERROR
        "KRN_SANITIZE='${KRN_SANITIZE}' is invalid — use off|address|thread|undefined|address+undefined")
    endif()
    add_compile_options(${_KRN_SAN_FLAGS})
    add_link_options(${_KRN_SAN_FLAGS})
    message(STATUS "Sanitizer enabled for all targets: ${KRN_SANITIZE}")
  endif()
endif()

# =======================
# Warning flags helper (ENC-806 / L12)
# =======================
# Applies the standard warning set to a target. -Werror stays OFF unless the
# opt-in KRN_WARNINGS_AS_ERRORS is set, so concurrent source edits never break
# the central build just by emitting a warning.
function(krn_apply_warnings tgt)
  if (MSVC)
    target_compile_options(${tgt} PRIVATE /W4)
    if (KRN_WARNINGS_AS_ERRORS)
      target_compile_options(${tgt} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
    if (KRN_WARNINGS_AS_ERRORS)
      target_compile_options(${tgt} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# =======================
# Boost (prefer normal install)
# =======================
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.75 QUIET COMPONENTS system)

set(USE_HEADER_ONLY_BOOST OFF)
if (NOT Boost_FOUND)
  # Fallback to header-only using your local unpacked folder
  set(BOOST_FALLBACK_INCLUDE "${THIRD_PARTY_ROOT}/boost_1_88_0")
  if (EXISTS "${BOOST_FALLBACK_INCLUDE}/boost/beast/core.hpp")
    message(STATUS "Using header-only Boost from: ${BOOST_FALLBACK_INCLUDE}")
    set(USE_HEADER_ONLY_BOOST ON)

    add_library(Boost::headers INTERFACE IMPORTED)
    target_include_directories(Boost::headers INTERFACE "${BOOST_FALLBACK_INCLUDE}")

    # Provide a stub 'Boost::system' since <boost/system/error_code.hpp> is header-only
    add_library(Boost::system INTERFACE IMPORTED)
    target_link_libraries(Boost::system INTERFACE Boost::headers)
  else()
    message(FATAL_ERROR
      "Boost not found and no fallback at ${BOOST_FALLBACK_INCLUDE}. "
      "Install Boost or set -DTHIRD_PARTY_ROOT=C:/thirdparty")
  endif()
else()
  message(STATUS "Found Boost: ${Boost_INCLUDE_DIRS}")
endif()

# =======================
# RapidJSON (header-only)
# =======================
# We want #include <rapidjson/document.h>
# Accept both:
#   C:/thirdparty/rapidjson            (contains folder 'rapidjson/')
#   C:/thirdparty/rapidjson/include    (contains folder 'rapidjson/')
set(RAPIDJSON_HINTS
  "${RAPIDJSON_ROOT}"
  "$ENV{RAPIDJSON_ROOT}"
  "${THIRD_PARTY_ROOT}/rapidjson"
  "${THIRD_PARTY_ROOT}/rapidjson/include"
)

find_path(RAPIDJSON_INCLUDE_DIR
  NAMES rapidjson/document.h
  HINTS ${RAPIDJSON_HINTS}
)

if (NOT RAPIDJSON_INCLUDE_DIR)
  message(FATAL_ERROR
    "RapidJSON headers not found. Set RAPIDJSON_ROOT or place them under "
    "${THIRD_PARTY_ROOT}/rapidjson so that ${RAPIDJSON_INCLUDE_DIR}/rapidjson/document.h exists.")
endif()

add_library(RapidJSON::rapidjson INTERFACE IMPORTED)
target_include_directories(RapidJSON::rapidjson INTERFACE "${RAPIDJSON_INCLUDE_DIR}")
message(STATUS "RapidJSON include dir: ${RAPIDJSON_INCLUDE_DIR}")

# =======================
# Sources / targets
# =======================
# Engine / connector split:
#   krn_engine             — domain-agnostic core (all src/**/*.cpp, minus main)
#   krn_connector_market   — market-specific code (connectors/market/**)
#   krn                    — INTERFACE alias linking both (legacy consumer target)
#   krn_server             — composition root, picks up main.cpp + both libs
# The split is enforced by include-directory scoping: krn_engine's PUBLIC
# include paths contain *only* the engine headers, so engine code that
# accidentally references a connector header fails to compile.

set(MAIN_SRC "${PROJECT_SOURCE_DIR}/src/main.cpp")

# Explicit lists, not globs (ENC-1551): a missing file is a configure error,
# and paths are rooted at this project, not at whoever add_subdirectory()s it.
set(KRN_ENGINE_SOURCES
  src/core/AtomicStore.cpp
  src/core/BuiltinFunctions.cpp
  src/core/Dispatcher.cpp
  src/core/DispatcherRecord.cpp
  src/core/Expr.cpp
  src/core/FunctionMap.cpp
  src/core/JsonValidator.cpp
  src/core/RequestRegistry.cpp
  src/core/TreeBuilder.cpp
  src/data/SymbolTable.cpp
  src/engine/IEventComputer.cpp
  src/forum/ConnectorsClient.cpp
  src/nodes/Aggregate.cpp
  src/nodes/AtomicAccessor.cpp
  src/nodes/BucketTime.cpp
  src/nodes/Expr.cpp
  src/nodes/Field.cpp
  src/nodes/Filter.cpp
  src/nodes/GroupSplit.cpp
  src/nodes/Interval.cpp
  src/nodes/Listener.cpp
  src/nodes/Pack.cpp
  src/nodes/Responder.cpp
  src/nodes/Switch.cpp
  src/nodes/Tee.cpp
  src/nodes/TumblingWindow.cpp
  src/nodes/VectorReducer.cpp
  src/nodes/Worker.cpp
  src/rt/Strand.cpp
  src/rt/ThreadPool.cpp
  src/server/ClientSession.cpp
  src/server/WebSocketServer.cpp
  src/util/Config.cpp
  src/util/Logger.cpp
  src/util/Metrics.cpp
  src/ws/ClientConnection.cpp
  src/ws/WSResponder.cpp
  src/ws/WsBridge.cpp
)
list(TRANSFORM KRN_ENGINE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")

set(KRN_ENGINE_HEADERS
  include/krn/AtomicStore.hpp
  include/krn/Dispatcher.hpp
  include/krn/Event.hpp
  include/krn/ExecutionContext.hpp
  include/krn/Expr.hpp
  include/krn/FunctionMap.hpp
  include/krn/FunctionRegistry.hpp
  include/krn/JsonValidator.hpp
  include/krn/Metrics.hpp
  include/krn/NodeRegistry.hpp
  include/krn/RequestRegistry.hpp
  include/krn/Result.hpp
  include/krn/Span.hpp
  include/krn/StreamValue.hpp
  include/krn/TreeBuilder.hpp
  include/krn/atomic/AtomicProviderRegistry.hpp
  include/krn/engine/ConfigNamespaceRegistry.hpp
  include/krn/engine/EngineRegistries.hpp
  include/krn/engine/EventComputerRegistry.hpp
  include/krn/engine/EventTypeRegistry.hpp
  include/krn/engine/FieldSource.hpp
  include/krn/engine/IConnector.hpp
  include/krn/engine/IEventComputer.hpp
  include/krn/engine/IngressRegistry.hpp
  include/krn/engine/JsonFieldSource.hpp
  include/krn/engine/NodeTypeRegistry.hpp
  include/krn/engine/Registries.hpp
  include/krn/engine/SymbolTable.hpp
  include/krn/forum/ConnectorsClient.hpp
  include/krn/nodes/Aggregate.hpp
  include/krn/nodes/AtomicAccessor.hpp
  include/krn/nodes/BucketTime.hpp
  include/krn/nodes/Expr.hpp
  include/krn/nodes/Field.hpp
  include/krn/nodes/Filter.hpp
  include/krn/nodes/GroupSplit.hpp
  include/krn/nodes/INode.hpp
  include/krn/nodes/InputPort.hpp
  include/krn/nodes/Interval.hpp
  include/krn/nodes/JoinBy.hpp
  include/krn/nodes/Listener.hpp
  include/krn/nodes/NodeEnv.hpp
  include/krn/nodes/Pack.hpp
  include/krn/nodes/Responder.hpp
  include/krn/nodes/Switch.hpp
  include/krn/nodes/Tee.hpp
  include/krn/nodes/TumblingWindow.hpp
  include/krn/nodes/VectorReducer.hpp
  include/krn/nodes/Worker.hpp
  include/krn/rt/SPSCQueue.hpp
  include/krn/rt/Strand.hpp
  include/krn/rt/ThreadPool.hpp
  include/krn/runtime/IStoppable.hpp
  include/krn/runtime/ShutdownCoordinator.hpp
  include/krn/server/ClientSession.hpp
  include/krn/server/RequestKey.hpp
  include/krn/server/WebSocketServer.hpp
  include/krn/util/Config.hpp
  include/krn/util/JsonUtil.hpp
  include/krn/util/Logger.hpp
  include/krn/util/Metrics.hpp
  include/krn/ws/ClientConnection.hpp
  include/krn/ws/WSResponder.hpp
  include/krn/ws/WsBridge.hpp
)
list(TRANSFORM KRN_ENGINE_HEADERS PREPEND "${PROJECT_SOURCE_DIR}/")

set(KRN_MARKET_SOURCES
  connectors/market/src/MarketConnector.cpp
  connectors/market/src/MarketIngress.cpp
  connectors/market/src/MarketTA.cpp
  connectors/market/src/book/OrderBook.cpp
  connectors/market/src/book/OrderBookManager.cpp
  connectors/market/src/feed/IFeedAdapter.cpp
  connectors/market/src/feed/ItchAdapter.cpp
  connectors/market/src/ob/ObKey.cpp
  connectors/market/src/ob/ObMaterializer.cpp
  connectors/market/src/ob/ObProvider.cpp
  connectors/market/src/server/FeedServer.cpp
  connectors/market/src/ws/WsFeedClient.cpp
)
list(TRANSFORM KRN_MARKET_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
set(KRN_MARKET_HEADERS
  connectors/market/include/krn/book/DepthTypes.hpp
  connectors/market/include/krn/book/OrderBook.hpp
  connectors/market/include/krn/book/OrderBookManager.hpp
  connectors/market/include/krn/feed/FeedEvent.hpp
  connectors/market/include/krn/feed/IFeedAdapter.hpp
  connectors/market/include/krn/feed/ItchAdapter.hpp
  connectors/market/include/krn/market/AtomicFunctions.hpp
  connectors/market/include/krn/market/FeedServer.hpp
  connectors/market/include/krn/market/MarketConnector.hpp
  connectors/market/include/krn/market/MarketFieldMap.hpp
  connectors/market/include/krn/market/MarketIngress.hpp
  connectors/market/include/krn/market/MarketTA.hpp
  connectors/market/include/krn/market/SymbolHistory.hpp
  connectors/market/include/krn/market/WsFeedClient.hpp
  connectors/market/include/krn/ob/FunctionalSnapshotSource.hpp
  connectors/market/include/krn/ob/ObKey.hpp
  connectors/market/include/krn/ob/ObMaterializer.hpp
  connectors/market/include/krn/ob/ObProvider.hpp
  connectors/market/include/krn/ob/ObSnapshot.hpp
  connectors/market/include/krn/ta/Indicators.hpp
)
list(TRANSFORM KRN_MARKET_HEADERS PREPEND "${PROJECT_SOURCE_DIR}/")

# ---- Engine library ----
add_library(krn_engine STATIC ${KRN_ENGINE_SOURCES} ${KRN_ENGINE_HEADERS})
target_include_directories(krn_engine PUBLIC "${PROJECT_SOURCE_DIR}/include")

if (Boost_FOUND)
  target_include_directories(krn_engine PUBLIC ${Boost_INCLUDE_DIRS})
  target_link_libraries(krn_engine PUBLIC Boost::system)
else()
  target_link_libraries(krn_engine PUBLIC Boost::system)
  target_compile_definitions(krn_engine PUBLIC
    BOOST_ALL_NO_LIB
    BOOST_ERROR_CODE_HEADER_ONLY
    BOOST_SYSTEM_NO_DEPRECATED
    BOOST_ASIO_NO_DEPRECATED
    BOOST_BEAST_USE_STD_STRING_VIEW
  )
endif()

target_link_libraries(krn_engine PUBLIC Threads::Threads RapidJSON::rapidjson)

if (OpenSSL_FOUND)
  target_link_libraries(krn_engine PUBLIC OpenSSL::SSL OpenSSL::Crypto)
  target_compile_definitions(krn_engine PUBLIC KRN_HAS_SSL=1)
endif()

# ---- Market connector library ----
add_library(krn_connector_market STATIC ${KRN_MARKET_SOURCES} ${KRN_MARKET_HEADERS})
target_include_directories(krn_connector_market PUBLIC
  "${PROJECT_SOURCE_DIR}/connectors/market/include"
)
target_link_libraries(krn_connector_market PUBLIC krn_engine)

# ---- Legacy umbrella alias so existing rules that link `krn` keep working ----
add_library(krn INTERFACE)
target_link_libraries(krn INTERFACE krn_engine krn_connector_market)

# ---- Synthetic connector (demo): proves the zero-core-changes invariant ----
# Not linked into `krn` — kept as an opt-in target that test code and demos
# pull in explicitly. Adding a connector requires (a) this library target and
# (b) one `registerWith` call at the composition root; no engine edits.
set(KRN_SYNTHETIC_SOURCES
  plugins/reference/src/ReferenceComputer.cpp
  plugins/reference/src/SyntheticConnector.cpp
)
list(TRANSFORM KRN_SYNTHETIC_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
set(KRN_SYNTHETIC_HEADERS
  plugins/reference/include/krn/reference/ReferenceComputer.hpp
  plugins/reference/include/krn/reference/SyntheticConnector.hpp
)
list(TRANSFORM KRN_SYNTHETIC_HEADERS PREPEND "${PROJECT_SOURCE_DIR}/")
add_library(krn_plugin_reference STATIC
  ${KRN_SYNTHETIC_SOURCES} ${KRN_SYNTHETIC_HEADERS})
target_include_directories(krn_plugin_reference PUBLIC
  "${PROJECT_SOURCE_DIR}/plugins/reference/include")
target_link_libraries(krn_plugin_reference PUBLIC krn_engine)

# Warnings — apply to every first-party target (ENC-806 / L12). Previously only
# krn_engine + krn_connector_market got -Wall/-Wextra; krn_plugin_reference,
# krn_server, krn_tests and the benchmarks were silent. krn_server/krn_tests/
# benchmarks are wired at their definition sites below.
krn_apply_warnings(krn_engine)
krn_apply_warnings(krn_connector_market)
krn_apply_warnings(krn_plugin_reference)

if (WIN32)
  add_definitions(-D_WIN32_WINNT=0x0A00 -DNOMINMAX)
endif()

# =======================
# Release optimizations
# =======================
if (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  if (MSVC)
    target_compile_options(krn_engine           PRIVATE /O2 /Ob2 /GL)
    target_compile_options(krn_connector_market PRIVATE /O2 /Ob2 /GL)
    target_link_options(krn_engine           PUBLIC /LTCG)
    target_link_options(krn_connector_market PUBLIC /LTCG)
  else()
    # Microarchitecture (ENC-790 / H5). -O3 always; -march is portable by default.
    set(KRN_RELEASE_ARCH_FLAGS "")
    if (KRN_NATIVE_ARCH)
      list(APPEND KRN_RELEASE_ARCH_FLAGS -march=native)
      message(STATUS "Release: -march=native (KRN_NATIVE_ARCH=ON) — binary pinned to THIS CPU")
    elseif (NOT KRN_ARCH_BASELINE STREQUAL "" AND
            CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
      list(APPEND KRN_RELEASE_ARCH_FLAGS "-march=${KRN_ARCH_BASELINE}")
      message(STATUS "Release: portable -march=${KRN_ARCH_BASELINE} (set KRN_NATIVE_ARCH=ON to target the build CPU)")
    else()
      message(STATUS "Release: no -march (portable default)")
    endif()
    target_compile_options(krn_engine           PRIVATE -O3 ${KRN_RELEASE_ARCH_FLAGS})
    target_compile_options(krn_connector_market PRIVATE -O3 ${KRN_RELEASE_ARCH_FLAGS})
  endif()

  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
  if (ipo_supported)
    set_property(TARGET krn_engine           PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_property(TARGET krn_connector_market PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    if (TARGET krn_server)
      set_property(TARGET krn_server PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    message(STATUS "LTO enabled for krn_engine + krn_connector_market + krn_server")
  else()
    message(STATUS "LTO not supported: ${ipo_output}")
  endif()
endif()

# App executable (if src/main.cpp exists)
if (EXISTS "${MAIN_SRC}")
  add_executable(krn_server "${MAIN_SRC}")
  target_include_directories(krn_server PRIVATE "${PROJECT_SOURCE_DIR}/include")
  if (Boost_FOUND)
    target_include_directories(krn_server PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(krn_server PRIVATE krn Boost::system)
  else()
    target_link_libraries(krn_server PRIVATE krn Boost::system)
    target_compile_definitions(krn_server PRIVATE
      BOOST_ALL_NO_LIB
      BOOST_ERROR_CODE_HEADER_ONLY
      BOOST_SYSTEM_NO_DEPRECATED
      BOOST_ASIO_NO_DEPRECATED
      BOOST_BEAST_USE_STD_STRING_VIEW
    )
  endif()
  target_link_libraries(krn_server PRIVATE Threads::Threads RapidJSON::rapidjson)
  krn_apply_warnings(krn_server)
endif()

# Tests (opt-in)
if (KRN_BUILD_TESTS)
  enable_testing()

  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  # krn_tests: the engine suite. Its bootstrap registers no connector, and it
  # links krn_engine, not the `krn` umbrella, so no market code reaches it
  # (ENC-1562; krn_engine_isolation check D enforces it).
  set(KRN_TEST_SOURCES
    tests/connection/ConnectionTest.cpp
    tests/connectors/SyntheticConnectorTest.cpp
    tests/context/ContextTest.cpp
    tests/core/AtomicStoreCapsTest.cpp
    tests/core/AtomicStoreTest.cpp
    tests/core/ConfigNamespaceDispatchTest.cpp
    tests/core/ConfigTest.cpp
    tests/core/ExprTest.cpp
    tests/core/LoggerTest.cpp
    tests/core/RecordValueTest.cpp
    tests/core/SelectFunctionTest.cpp
    tests/core/ThreadPoolTest.cpp
    tests/dispatch/AtomicKeyNamespaceTest.cpp
    tests/dispatch/ClockCoherentPullTest.cpp
    tests/dispatch/DispatchTest.cpp
    tests/dispatch/InjectedAtomicTest.cpp
    tests/dispatch/RecordIngressEngineTest.cpp
    tests/engine/ConnectorLifecycleTest.cpp
    tests/engine/EngineRegistriesShapeTest.cpp
    tests/engine/EventComputerCacheTest.cpp
    tests/engine/IngressFactoryDispatchTest.cpp
    tests/engine/RegistriesTest.cpp
    tests/engine/SymbolTableTest.cpp
    tests/feed/GapFixesTest.cpp
    tests/forum/ConnectorsClientTest.cpp
    tests/integration/ConcurrencyContentionTest.cpp
    tests/integration/Enc101CanonicalPatternTest.cpp
    tests/integration/ExpressionLanguageCapstoneTest.cpp
    tests/integration/LetBindingsTest.cpp
    tests/integration/OrderedDeliveryTest.cpp
    tests/integration/StressTest.cpp
    tests/mapping/FunctionMapTest.cpp
    tests/nodes/AggregateTest.cpp
    tests/nodes/AtomicAccessorTest.cpp
    tests/nodes/BucketStartStampTest.cpp
    tests/nodes/BucketTimeTest.cpp
    tests/nodes/ExprNodeTest.cpp
    tests/nodes/FieldTest.cpp
    tests/nodes/FilterTest.cpp
    tests/nodes/GroupSplitTest.cpp
    tests/nodes/IntervalTest.cpp
    tests/nodes/ListenerTest.cpp
    tests/nodes/PackTest.cpp
    tests/nodes/ResponderTest.cpp
    tests/nodes/SwitchTest.cpp
    tests/nodes/TeeTest.cpp
    tests/nodes/TumblingWindowTest.cpp
    tests/nodes/VectorReducerTest.cpp
    tests/nodes/WindowReducerCompositionTest.cpp
    tests/nodes/WorkerTest.cpp
    tests/registry/RegistryTest.cpp
    tests/server/RequestKeyHashTest.cpp
    tests/test_bootstrap.cpp
    tests/treebuilder/ComposedChainTest.cpp
    tests/treebuilder/CorpusTest.cpp
    tests/treebuilder/FanInArityTest.cpp
    tests/treebuilder/FanInJoinKeyTest.cpp
    tests/treebuilder/RecordTerminalTest.cpp
    tests/treebuilder/TreeBuilderTest.cpp
    tests/validation/ValidationTest.cpp
    tests/ws/ClientSessionBackpressureTest.cpp
    tests/ws/ClientSessionTest.cpp
    tests/ws/SubscriptionStrandMintTest.cpp
    tests/ws/WSResponderTest.cpp
    tests/ws/WebSocketE2ETest.cpp
    tests/ws/WsBridgeTest.cpp
  )
  list(TRANSFORM KRN_TEST_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
  add_executable(krn_tests ${KRN_TEST_SOURCES})
  target_include_directories(krn_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
  if (Boost_FOUND)
    target_include_directories(krn_tests PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(krn_tests PRIVATE krn_engine Boost::system)
  else()
    target_link_libraries(krn_tests PRIVATE krn_engine Boost::system)
    target_compile_definitions(krn_tests PRIVATE
      BOOST_ALL_NO_LIB
      BOOST_ERROR_CODE_HEADER_ONLY
      BOOST_SYSTEM_NO_DEPRECATED
      BOOST_ASIO_NO_DEPRECATED
      BOOST_BEAST_USE_STD_STRING_VIEW
    )
  endif()
  target_link_libraries(krn_tests PRIVATE
    Threads::Threads
    RapidJSON::rapidjson
    GTest::gtest_main
    krn_plugin_reference
  )
  krn_apply_warnings(krn_tests)

  # CorpusTest data (ENC-807 / L17): copy the corpus next to the test binary and
  # run the test from that directory, so CorpusTest can always locate
  # corpus_requests.json (it otherwise GTEST_SKIPs / fails on the missing file).
  add_custom_command(TARGET krn_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${PROJECT_SOURCE_DIR}/tests/treebuilder/corpus_requests.json"
      "$<TARGET_FILE_DIR:krn_tests>/corpus_requests.json"
    COMMENT "Copying corpus_requests.json next to krn_tests")

  # ENC-1340: let the binary find its corpus without help from the cwd.
  # The three historical search paths were all cwd-relative, so `ctest` (which
  # sets WORKING_DIRECTORY, above) always worked and a hand-run of the raw
  # binary from anywhere else produced a dozen failures that looked like
  # TreeBuilder bugs. tests/support/CorpusPath.hpp adds two cwd-independent
  # candidates: the directory of the running executable, and this one — the
  # configure-time source tree. Between them a binary built by this project
  # finds the corpus from any directory, and the diagnostic it prints when it
  # genuinely cannot names the cwd and every path tried.
  target_compile_definitions(krn_tests PRIVATE
    KRN_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

  # ─── ENC-1289's EXPECTED-FAILURE MACHINERY IS GONE. ENC-1292 CLEARED THE
  # LAST GATE, 2026-09-20.
  #
  # What stood here: `tests/treebuilder/CorpusTest.cpp`'s value assertions
  # (SPEC specs/2026-09-20-gma-join-correctness D8) were red on `master` BY
  # DESIGN — an instrument that cannot fail is not a gate — so each was
  # registered as its own ctest case with `WILL_FAIL TRUE`, excluded from the
  # main `krn_tests` case by a `--gtest_filter=-...` list, and guarded by
  # `gma_enc1289_expected_failures_really_failed`, which pinned the COUNT and
  # required gtest's own end-of-run summary line so a crash could not read as
  # an expected failure. It unwound itself by construction: each fix flipped
  # its WILL_FAIL case RED and forced whoever landed it to come here.
  #
  # It did exactly that, three times, and the count is the record:
  #   3  ENC-1289 (the instrument lands first, D8)
  #   2  ENC-1005  — the per-request strand; Corpus86_SpreadIsExactlyTwoCents
  #   1  ENC-1291  — port-indexed fan-in; Corpus86_JoinMustNotComplete...
  #   0  ENC-1292  — the declared join key; Corpus87_CrossSymbolJoin...
  #
  # ENC-1291 left the last one deliberately: port-indexing turned "six tuples
  # pairing one symbol with itself" into "no tuples at all", so the test went
  # green VACUOUSLY — every assertion true over an empty tuple list. It added
  # an anti-vacuity ASSERT and kept the marker rather than bank that. ENC-1292
  # declares `by:"none"` on a copy of the corpus entry, the join emits six
  # MIXED tuples, and the assertion is now checking a non-empty set. All three
  # corpus 87 tests — the gate, its observation-point control, and the
  # cross-corpus measurement in `tests/treebuilder/FanInJoinKeyTest.cpp` —
  # now run inside the ordinary `krn_tests` case below, uninverted.
  #
  # NOTE (unchanged and still true): the ENC-1065 `--gtest_repeat` check must
  # be run against the RAW, UNFILTERED binary (`./krn_tests --gtest_repeat=N`).
  # It always had to be; there is simply no filter left for it to dodge.
  add_test(NAME krn_tests
    COMMAND krn_tests
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:krn_tests>")

  # krn_market_tests (ENC-1562, purity SPEC D13): every case that needs the
  # market connector, with a market bootstrap that registers MarketConnector.
  # The 8 COMMON files plus tests/market/; krn-common imports exactly this set
  # (§6 row 18). Top-level only, like everything in this block.
  set(KRN_MARKET_TEST_SOURCES
    tests/book/CrossedBookTest.cpp
    tests/book/OrderBookTest.cpp
    tests/feed/FeedEventDispatchTest.cpp
    tests/feed/ItchAdapterTest.cpp
    tests/feed/ItchSemanticTest.cpp
    tests/ob/ObKeyTest.cpp
    tests/ob/ObMaterializerTest.cpp
    tests/ob/ObProviderTest.cpp
    tests/market/AtomicFunctionsTest.cpp
    tests/market/AtomicKeyNamespaceMarketTest.cpp
    tests/market/BootstrapIdempotenceTest.cpp
    tests/market/ClockCoherentCorpusSweepTest.cpp
    tests/market/ConnectorLifecycleRealTest.cpp
    tests/market/GapFixesTest.cpp
    tests/market/IndicatorsTest.cpp
    tests/market/IntegrationTest.cpp
    tests/market/MarketFieldMapTest.cpp
    tests/market/MarketTANumericTest.cpp
    tests/market/test_bootstrap.cpp
  )
  list(TRANSFORM KRN_MARKET_TEST_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
  add_executable(krn_market_tests ${KRN_MARKET_TEST_SOURCES})
  target_include_directories(krn_market_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
  if (Boost_FOUND)
    target_include_directories(krn_market_tests PRIVATE ${Boost_INCLUDE_DIRS})
  else()
    target_compile_definitions(krn_market_tests PRIVATE
      BOOST_ALL_NO_LIB
      BOOST_ERROR_CODE_HEADER_ONLY
      BOOST_SYSTEM_NO_DEPRECATED
      BOOST_ASIO_NO_DEPRECATED
      BOOST_BEAST_USE_STD_STRING_VIEW
    )
  endif()
  target_link_libraries(krn_market_tests PRIVATE
    krn
    Boost::system
    Threads::Threads
    RapidJSON::rapidjson
    GTest::gtest_main
    krn_plugin_reference
  )
  krn_apply_warnings(krn_market_tests)
  add_custom_command(TARGET krn_market_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${PROJECT_SOURCE_DIR}/tests/treebuilder/corpus_requests.json"
      "$<TARGET_FILE_DIR:krn_market_tests>/corpus_requests.json"
    COMMENT "Copying corpus_requests.json next to krn_market_tests")
  target_compile_definitions(krn_market_tests PRIVATE
    KRN_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  add_test(NAME krn_market_tests
    COMMAND krn_market_tests
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:krn_market_tests>")

  # ENC-1006: an occupied wsPort/feedPort must produce a clean non-zero exit,
  # not std::terminate. This drives the real krn_server binary, so it lives as
  # a script test rather than a gtest case.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if (TARGET krn_server AND Python3_Interpreter_FOUND)
    add_test(NAME krn_port_conflict
      COMMAND "${Python3_EXECUTABLE}"
              "${PROJECT_SOURCE_DIR}/tests/scripts/port_conflict_test.py"
              "$<TARGET_FILE:krn_server>")
    set_tests_properties(krn_port_conflict PROPERTIES TIMEOUT 120)

    # ENC-1331: `krn_server <ws> <conf> <feed>` must BIND <feed>. Asserted
    # against the live acceptor rather than the boot log, because the log
    # already printed the right number while the socket used the file's.
    add_test(NAME krn_feed_port_override
      COMMAND "${Python3_EXECUTABLE}"
              "${PROJECT_SOURCE_DIR}/tests/scripts/feed_port_override_test.py"
              "$<TARGET_FILE:krn_server>")
    set_tests_properties(krn_feed_port_override PROPERTIES TIMEOUT 120)
  elseif (NOT Python3_Interpreter_FOUND)
    message(STATUS "krn_port_conflict / krn_feed_port_override tests skipped: no Python3 interpreter")
  endif()
endif()

# Benchmarks (opt-in)
if (KRN_BUILD_BENCHMARKS)
  include(FetchContent)
  FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.8.3
  )
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googlebenchmark)

  # Helper: create a benchmark executable from a single source file
  function(krn_add_benchmark NAME SRC)
    add_executable(${NAME} ${SRC})
    target_include_directories(${NAME} PRIVATE "${PROJECT_SOURCE_DIR}/include")
    if (Boost_FOUND)
      target_include_directories(${NAME} PRIVATE ${Boost_INCLUDE_DIRS})
      target_link_libraries(${NAME} PRIVATE krn Boost::system)
    else()
      target_link_libraries(${NAME} PRIVATE krn Boost::system)
      target_compile_definitions(${NAME} PRIVATE
        BOOST_ALL_NO_LIB
        BOOST_ERROR_CODE_HEADER_ONLY
        BOOST_SYSTEM_NO_DEPRECATED
        BOOST_ASIO_NO_DEPRECATED
        BOOST_BEAST_USE_STD_STRING_VIEW
      )
    endif()
    target_link_libraries(${NAME} PRIVATE
      Threads::Threads
      RapidJSON::rapidjson
      benchmark::benchmark
    )
    krn_apply_warnings(${NAME})
  endfunction()

  krn_add_benchmark(bench_atomic_functions  "${PROJECT_SOURCE_DIR}/benchmarks/AtomicFunctionsBench.cpp")
  krn_add_benchmark(bench_dispatcher        "${PROJECT_SOURCE_DIR}/benchmarks/DispatcherBench.cpp")
  krn_add_benchmark(bench_atomic_store      "${PROJECT_SOURCE_DIR}/benchmarks/AtomicStoreBench.cpp")
  krn_add_benchmark(bench_thread_pool       "${PROJECT_SOURCE_DIR}/benchmarks/ThreadPoolBench.cpp")
  krn_add_benchmark(bench_window_nodes      "${PROJECT_SOURCE_DIR}/benchmarks/WindowNodesBench.cpp")

  # Convenience target: build all benchmarks at once
  add_custom_target(krn_benchmarks DEPENDS
    bench_atomic_functions
    bench_dispatcher
    bench_atomic_store
    bench_thread_pool
    bench_window_nodes
  )
endif()

# =======================
# Data layer (ENC-1481)
# =======================
# After the tests block on purpose: data/CMakeLists.txt registers its own ctest
# case against GTest::gtest_main, which only exists once the block above has
# run FetchContent on googletest.
if (KRN_BUILD_DATA)
  add_subdirectory(data)
else()
  message(STATUS
    "krn_data: DISABLED (KRN_BUILD_DATA=OFF) — SPEC D14's live-only client "
    "variant. No Arrow is fetched or built, and krn_data_tests is not "
    "registered with ctest. krn_engine_isolation still runs, on its "
    "krn_isolation_control positive control.")
endif()

# =======================
# Engine isolation gate (ENC-1481)
# =======================
# THE INVARIANT: krn_engine must not acquire a dependency on Arrow, the
# ClickHouse client, or the krn_data library target (SPEC D14,
# docs/data-layer.md §4 rule 6). SPEC D14's live-only client variant omits
# krn_data entirely and must still link, so this is a shipping constraint and
# not a style preference.
#
# It is checked rather than claimed because every way of breaking it still
# compiles and still passes every existing test: one `target_link_libraries`
# line would make the live-only variant impossible and nothing would say so.
#
# ONE ROW PER FIRST-PARTY TARGET, BECAUSE THE CHECK WALKS THE CLOSURE.
# krn_server links the `krn` umbrella and nothing else, so reading only
# krn_engine's and krn_server's own LINK_LIBRARIES is blind to exactly the line
# that breaks the invariant: `target_link_libraries(krn INTERFACE krn_data)`,
# or `krn::arrow` on a connector `krn` pulls in. Both leave the two roots'
# properties unchanged and both make KRN_BUILD_DATA=OFF unlinkable. The
# checker therefore reads a link GRAPH — `link.<target>` /
# `interface_link.<target>` — and walks it transitively from krn_engine and
# krn_server. Add a first-party library target here when you add one; a target
# with no row is treated as a leaf and only pattern-matched, which is correct
# for system and imported libraries and wrong for ours.
#
# THE CHECKED ROOTS are krn_engine, krn_server and krn_tests. krn_tests is
# there because data/CMakeLists.txt names "the engine test suite is built
# against an Arrow-free include path" as the thing being protected, and a
# prose claim with no check behind it is what finding 1 was about.
#
# THE POSITIVE CONTROL IS krn_isolation_control (ENC-1507, purity SPEC Q7).
# Its closure must trip a forbidden pattern, or the patterns match nothing in
# this build and a clean verdict about krn_engine is a verdict from a search
# that cannot find anything. It is declared below, in this file, so it exists
# in every configuration the gate runs in -- KRN_BUILD_DATA=OFF included, and
# after data/ leaves this repo. A missing or silent control is exit 2 (could
# not run), never a pass. krn_data's own row is a second control when present,
# and is cross-checked against KRN_BUILD_DATA.
#
# THE RESOLVED RAPIDJSON PATH IS IN THE MANIFEST (ENC-1497). `/third_party/`
# is gitignored, so it is populated in the shared checkout and absent in a
# fresh `git worktree`; find_path above therefore resolves RapidJSON to
# ${PROJECT_SOURCE_DIR}/third_party/rapidjson in one and to /usr/include in the
# other. That made ONE COMMIT FAIL THIS GATE FROM ONE CHECKOUT AND PASS FROM
# THE OTHER, with nothing in either run's output to explain the difference.
# Check B now accepts both (bounded -- see the checker's docstring), and the
# path is reported either way, because a verdict that depends on the checkout
# has to print the thing that differs.
#
# The manifest is produced with file(GENERATE) rather than passed on the
# command line because the properties are generator expressions, and because a
# resolved list on an add_test COMMAND would be split into separate arguments
# at every semicolon.
if (KRN_BUILD_TESTS AND TARGET krn_server)
  set(KRN_ISOLATION_MANIFEST "${PROJECT_BINARY_DIR}/krn_engine_isolation.manifest")

  # The gate's positive control. Scanned, never linked: the checker fails if
  # any other target's closure reaches it. The stub carries no library, so a
  # link to it would compile silently -- which is why that is checked.
  add_library(arrow::arrow_control_probe INTERFACE IMPORTED)
  add_library(krn_isolation_control INTERFACE)
  target_link_libraries(krn_isolation_control
    INTERFACE arrow::arrow_control_probe)

  # THE TARGET LIST IS ENUMERATED FROM CMAKE, NOT HAND-WRITTEN. A hand-written
  # list reproduces the original hole one level deeper: a new first-party
  # library would arrive with no row, be treated by the checker as a leaf, and
  # have its own dependencies never walked — so `krn_store -> krn_data` would
  # pass green exactly the way `krn -> krn_data` used to. BUILDSYSTEM_TARGETS
  # registers a target by its existing. UTILITY targets are dropped because
  # they have no link line. (CMake does not report IMPORTED or ALIAS targets
  # here, which is why the checker ALSO fails on any first-party-looking name
  # that has no row — belt and braces, and the braces are producer-independent.)
  set(_krn_iso_dirs "${PROJECT_SOURCE_DIR}")
  if (KRN_BUILD_DATA)
    list(APPEND _krn_iso_dirs "${PROJECT_SOURCE_DIR}/data")
  endif()
  set(_krn_iso_link_targets "")
  foreach (_krn_iso_dir IN LISTS _krn_iso_dirs)
    get_directory_property(_krn_iso_dir_targets
      DIRECTORY "${_krn_iso_dir}" BUILDSYSTEM_TARGETS)
    foreach (_krn_iso_t IN LISTS _krn_iso_dir_targets)
      get_target_property(_krn_iso_type ${_krn_iso_t} TYPE)
      if (NOT _krn_iso_type STREQUAL "UTILITY")
        list(APPEND _krn_iso_link_targets ${_krn_iso_t})
      endif()
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES _krn_iso_link_targets)
  list(SORT _krn_iso_link_targets)

  set(_krn_iso_link_rows "")
  foreach (_krn_iso_t IN LISTS _krn_iso_link_targets)
    string(APPEND _krn_iso_link_rows
"link.${_krn_iso_t}=$<JOIN:$<TARGET_PROPERTY:${_krn_iso_t},LINK_LIBRARIES>,|>
interface_link.${_krn_iso_t}=$<JOIN:$<TARGET_PROPERTY:${_krn_iso_t},INTERFACE_LINK_LIBRARIES>,|>
")
  endforeach()
  message(STATUS
    "krn_engine_isolation: link graph covers ${_krn_iso_link_targets}")

  file(GENERATE OUTPUT "${KRN_ISOLATION_MANIFEST}" CONTENT
"# Generated by CMake for tools/check_engine_isolation.py (ENC-1481). Do not edit.
root=${PROJECT_SOURCE_DIR}
engine_include_dirs=$<JOIN:$<TARGET_PROPERTY:krn_engine,INCLUDE_DIRECTORIES>,|>
engine_interface_include_dirs=$<JOIN:$<TARGET_PROPERTY:krn_engine,INTERFACE_INCLUDE_DIRECTORIES>,|>
engine_cmake_sources=$<JOIN:$<TARGET_PROPERTY:krn_engine,SOURCES>,|>
krn_data_enabled=${KRN_BUILD_DATA}
rapidjson_include_dir=${RAPIDJSON_INCLUDE_DIR}
${_krn_iso_link_rows}")

  if (Python3_Interpreter_FOUND)
    add_test(NAME krn_engine_isolation
      COMMAND "${Python3_EXECUTABLE}"
              "${PROJECT_SOURCE_DIR}/tools/check_engine_isolation.py"
              --manifest "${KRN_ISOLATION_MANIFEST}")
    set_tests_properties(krn_engine_isolation PROPERTIES TIMEOUT 120)

    # The gate's own gate. A predicate that stopped detecting its violation
    # would turn krn_engine_isolation into a green no-op; this runs each one
    # against an input that violates it and fails unless it is caught. The
    # anti-vacuity rule of docs/data-layer.md §5.4, applied to the checker.
    add_test(NAME krn_engine_isolation_selftest
      COMMAND "${Python3_EXECUTABLE}"
              "${PROJECT_SOURCE_DIR}/tools/check_engine_isolation.py"
              --selftest)
    set_tests_properties(krn_engine_isolation_selftest PROPERTIES TIMEOUT 120)
  else()
    message(WARNING
      "krn_engine_isolation test SKIPPED: no Python3 interpreter. The SPEC D14 "
      "engine-isolation invariant is NOT being checked in this build.")
  endif()
elseif (KRN_BUILD_TESTS)
  message(WARNING
    "krn_engine_isolation test SKIPPED: no krn_server target (src/main.cpp is "
    "absent), so check D cannot read its link libraries. The SPEC D14 "
    "engine-isolation invariant is NOT being checked in this build.")
endif()

# =======================
# L1 schema gate (ENC-1480)
# =======================
# THE INVARIANT: data/schema/clickhouse.sql must not COMPUTE. No aggregate
# function, no GROUP BY, no MATERIALIZED/ALIAS column, no view, no
# Aggregating/SummingMergeTree, no window function (SPEC D2; docs/data-layer.md
# §8). D2 is usually read as a rule about the shim's SELECTs, but it binds the
# schema just as hard and in a way no SELECT lint can reach: a MATERIALIZED
# column or an AggregatingMergeTree computes at insert or at merge time, so by
# the time any query runs the second implementation of gma's math has already
# produced its value.
#
# REGISTERED HERE BECAUSE A CHECKER NOTHING INVOKES IS NOT A GATE. It shipped
# with ENC-1480 as a standalone script, which means it was a gate exactly as
# often as somebody remembered to type it.
#
# NOT GATED ON KRN_BUILD_DATA, DELIBERATELY. The check reads a text file: no
# Arrow, no ClickHouse client, no C++ target, no server. Gating it on
# KRN_BUILD_DATA would mean SPEC D14's live-only client variant silently stops
# checking the invariant — and the schema file still ships in that variant, so
# the thing being protected is still there while the protection is not. It runs
# whenever tests are on.
#
# TWO CASES, because one of them is the other's gate. The lint alone could decay
# into a green no-op in several ways the lint itself cannot see: the aggregate
# vocabulary going empty, the comment stripper eating the statements, the
# statement splitter matching nothing. `self-test` runs the checker against
# seven fixtures and nine generated mutants of the real schema — each asserting
# its substitution changed the text before asserting the check reddens — so the
# gate is observed failing on every pass. Same anti-vacuity rule as
# krn_engine_isolation_selftest above (docs/data-layer.md §5.4).
#
# The schema path is passed explicitly rather than left to the script's own
# __file__ resolution so that a wrong path is a loud failure (exit 2, "could not
# run") instead of a case that quietly checks whatever it found.
if (KRN_BUILD_TESTS)
  if (Python3_Interpreter_FOUND)
    add_test(NAME krn_schema_no_compute
      COMMAND "${Python3_EXECUTABLE}"
              "${PROJECT_SOURCE_DIR}/data/schema/check_no_compute.py"
              lint "${PROJECT_SOURCE_DIR}/data/schema/clickhouse.sql")
    set_tests_properties(krn_schema_no_compute PROPERTIES TIMEOUT 120)

    # The gate's own gate — see the note above.
    add_test(NAME krn_schema_no_compute_selftest
      COMMAND "${Python3_EXECUTABLE}"
              "${PROJECT_SOURCE_DIR}/data/schema/check_no_compute.py"
              self-test)
    set_tests_properties(krn_schema_no_compute_selftest PROPERTIES TIMEOUT 120)
  else()
    message(WARNING
      "krn_schema_no_compute test SKIPPED: no Python3 interpreter. The SPEC D2 "
      "invariant that data/schema/clickhouse.sql computes nothing — no "
      "aggregate, no GROUP BY, no MATERIALIZED/ALIAS column, no "
      "Aggregating/SummingMergeTree — is NOT being checked in this build.")
  endif()
endif()
