# Minimal FetchContent consumer of the engine (ENC-1553). Driven by
# tests/consumer/run_consumer_smoke.cmake; not part of the engine's build.
cmake_minimum_required(VERSION 3.20)
project(krn_consumer_fetchcontent LANGUAGES CXX)

# The consumer's OWN options, spelled like the engine's. The driver passes
# -D...=ON for all three; the engine must not act on them and must not change
# them for the consumer.
option(KRN_BUILD_TESTS "the consumer's own option" OFF)
option(KRN_BUILD_DATA "the consumer's own option" OFF)
option(KRN_BUILD_BENCHMARKS "the consumer's own option" OFF)

include(FetchContent)
FetchContent_Declare(krn
  GIT_REPOSITORY "${KRN_SMOKE_URL}"
  GIT_TAG        "${KRN_SMOKE_TAG}"
)
FetchContent_MakeAvailable(krn)

set(_fail "")
macro(_check cond msg)
  if (${cond})
    message(STATUS "KRN_SMOKE ok: ${msg}")
  else()
    message(STATUS "KRN_SMOKE FAIL: ${msg}")
    list(APPEND _fail "${msg}")
  endif()
endmacro()

# ---- the fetched tree is a clone at the tag, not the checkout itself ----
file(REAL_PATH "${krn_SOURCE_DIR}" _krn_src)
file(REAL_PATH "${KRN_SMOKE_CHECKOUT}" _checkout)
string(FIND "${_krn_src}/" "${_checkout}/" _pos)
_check("NOT _pos EQUAL 0" "fetched tree ${_krn_src} is not the engine checkout")
execute_process(COMMAND git -C "${_krn_src}" rev-parse HEAD
  OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _rc)
_check("_rc EQUAL 0 AND _head STREQUAL KRN_SMOKE_TAG"
  "fetched tree is at ${KRN_SMOKE_TAG} (HEAD ${_head})")

# ---- (a) krn_engine's SOURCES all sit under the fetched tree ----
_check("TARGET krn_engine" "krn_engine exists")
if (TARGET krn_engine)
  get_target_property(_srcs krn_engine SOURCES)
  list(LENGTH _srcs _n)
  _check("_n GREATER 0" "(a) krn_engine has ${_n} SOURCES")
  set(_outside "")
  foreach (_s IN LISTS _srcs)
    file(REAL_PATH "${_s}" _rs BASE_DIRECTORY "${_krn_src}")
    string(FIND "${_rs}" "${_krn_src}/" _p)
    if (NOT _p EQUAL 0)
      list(APPEND _outside "${_rs}")
    endif()
  endforeach()
  _check("_outside STREQUAL \"\""
    "(a) every krn_engine SOURCE is under the fetched tree; outside: [${_outside}]")
endif()

# ---- (b) the engine defines its library targets and nothing else ----
# Every directory the engine added (data/, googletest's, ...) is walked.
set(_dirs "${krn_SOURCE_DIR}")
set(_all "")
set(_imported "")
while (_dirs)
  list(POP_FRONT _dirs _d)
  get_directory_property(_t DIRECTORY "${_d}" BUILDSYSTEM_TARGETS)
  get_directory_property(_i DIRECTORY "${_d}" IMPORTED_TARGETS)
  get_directory_property(_sub DIRECTORY "${_d}" SUBDIRECTORIES)
  list(APPEND _all ${_t})
  list(APPEND _imported ${_i})
  list(APPEND _dirs ${_sub})
endwhile()
list(SORT _all)
set(_expected ${KRN_SMOKE_EXPECTED_TARGETS})
list(SORT _expected)
_check("_all STREQUAL _expected"
  "(b) engine targets are exactly [${_expected}]; got [${_all}]")
foreach (_t IN ITEMS krn_connector_market krn_server krn_tests krn_data
                     krn_ingest krn_data_tests krn krn_isolation_control
                     krn_benchmarks gtest gtest_main benchmark)
  _check("NOT TARGET ${_t}" "(b) no target ${_t}")
endforeach()
# The control's IMPORTED stub is directory-scoped, so `if(TARGET)` from here
# cannot see it: read the engine's IMPORTED_TARGETS, positive control first.
_check("\"RapidJSON::rapidjson\" IN_LIST _imported"
  "(b) IMPORTED_TARGETS is readable (RapidJSON::rapidjson listed)")
_check("NOT \"arrow::arrow_control_probe\" IN_LIST _imported"
  "(b) the isolation control stub arrow::arrow_control_probe is not defined")

# ---- (c) the consumer's own cache and scope are untouched ----
foreach (_o IN ITEMS KRN_BUILD_TESTS KRN_BUILD_DATA KRN_BUILD_BENCHMARKS)
  _check("$CACHE{${_o}} AND ${_o}"
    "(c) consumer's ${_o} still ON (cache '$CACHE{${_o}}', scope '${${_o}}')")
endforeach()
_check("NOT DEFINED _krn_top_level" "(c) no engine-internal variable leaked")

if (_fail)
  list(LENGTH _fail _nf)
  message(FATAL_ERROR "KRN_SMOKE: ${_nf} assertion(s) failed")
endif()

add_executable(krn_consumer_probe src/main.cpp)
target_link_libraries(krn_consumer_probe PRIVATE krn::krn_engine)
