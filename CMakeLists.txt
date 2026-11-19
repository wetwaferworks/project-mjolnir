# Project Mjolnir, stage M0: foundations on toy weights.
# Standalone build, C17, no third-party libraries.
cmake_minimum_required(VERSION 3.16)
project(mjolnir C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(MJ_SANITIZE "Build with AddressSanitizer (and UBSan where the compiler has it)" OFF)
option(MJ_LIBFUZZER "Also build libFuzzer targets (clang or MSVC)" OFF)
set(MJ_FUZZ_ITERATIONS 100000 CACHE STRING "Inputs per target for the built-in fuzz test")

if(MSVC)
  set(MJ_WARN /W4 /WX /wd4996)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
else()
  set(MJ_WARN -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wvla -Werror)
endif()

if(MJ_SANITIZE)
  if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:/fsanitize=address> $<$<COMPILE_LANGUAGE:C>:/Zi>)
    add_link_options(/INCREMENTAL:NO /DEBUG)
  else()
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fsanitize=address,undefined> $<$<COMPILE_LANGUAGE:C>:-fno-sanitize-recover=all>
                        $<$<COMPILE_LANGUAGE:C>:-fno-omit-frame-pointer> $<$<COMPILE_LANGUAGE:C>:-g>)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

set(MJ_SOURCES
  src/mj_common.c
  src/mj_hash.c
  src/mj_fs.c
  src/mj_store.c
  src/mj_ledger.c
  src/mj_score.c
  src/mj_toy.c
  src/mj_synth.c
  src/mj_dream.c
  src/mj_file.c
  src/mj_safet.c
  src/mj_delta.c
  src/mj_apply.c
  src/mj_workcopy.c
  src/mj_session.c)

# The GPU path for applying edits to real weights. Off by default; the CPU
# path gives the same bytes.
option(MJ_CUDA "Build the CUDA path for applying edits (needs the CUDA toolkit)" OFF)
if(MJ_CUDA)
  if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 86)
  endif()
  enable_language(CXX)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  set(MJ_GPU_SOURCES src/mj_apply_cuda.cu)
else()
  set(MJ_GPU_SOURCES src/mj_apply_nocuda.c)
endif()

add_library(mjolnir STATIC ${MJ_SOURCES} ${MJ_GPU_SOURCES})
target_include_directories(mjolnir PUBLIC include)
target_compile_options(mjolnir PRIVATE $<$<COMPILE_LANGUAGE:C>:${MJ_WARN}>)
if(NOT MSVC)
  # the apply path's sums must round step by step, never fused
  target_compile_options(mjolnir PRIVATE $<$<COMPILE_LANGUAGE:C>:-ffp-contract=off>)
  find_package(Threads REQUIRED)
  target_link_libraries(mjolnir PUBLIC Threads::Threads m)
endif()
if(MJ_CUDA)
  set_source_files_properties(src/mj_apply_cuda.cu PROPERTIES LANGUAGE CUDA)
  target_compile_options(mjolnir PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-fmad=false -prec-div=true -prec-sqrt=true -ftz=false>)
  if(MSVC)
    # nvcc's generated host stub trips C4211; it is not our code
    target_compile_options(mjolnir PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/wd4211>)
  endif()
  target_link_libraries(mjolnir PUBLIC CUDA::cudart_static)
  target_compile_definitions(mjolnir PUBLIC MJ_HAVE_CUDA=1)
endif()

foreach(tool mj_nap mj_nightly mj_ledger_verify mj_real)
  add_executable(${tool} tools/${tool}.c)
  target_link_libraries(${tool} PRIVATE mjolnir)
  target_compile_options(${tool} PRIVATE ${MJ_WARN})
endforeach()

# ---- test bench ----
enable_testing()
set(MJ_TEST_TMP ${CMAKE_BINARY_DIR}/test-tmp)
file(MAKE_DIRECTORY ${MJ_TEST_TMP})
set(MJ_TEST_ENV "MJ_TEST_TMP=${MJ_TEST_TMP}")

foreach(t test_hash test_store test_ledger test_score test_update test_dream test_safet test_delta test_workcopy)
  add_executable(${t} tests/${t}.c)
  target_link_libraries(${t} PRIVATE mjolnir)
  target_include_directories(${t} PRIVATE tests)
  if(NOT MSVC)
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-format-truncation)
  endif()
  add_test(NAME ${t} COMMAND ${t})
  set_tests_properties(${t} PROPERTIES ENVIRONMENT "${MJ_TEST_ENV}" TIMEOUT 120)
endforeach()

add_executable(mj_fuzz tests/fuzz/mj_fuzz.c)
target_link_libraries(mj_fuzz PRIVATE mjolnir)
target_include_directories(mj_fuzz PRIVATE tests/fuzz)
add_test(NAME fuzz_builtin COMMAND mj_fuzz ${MJ_FUZZ_ITERATIONS})
set_tests_properties(fuzz_builtin PROPERTIES ENVIRONMENT "${MJ_TEST_ENV}" TIMEOUT 600)

if(MJ_LIBFUZZER)
  # a second copy of the library with coverage feedback for the fuzzer
  add_library(mjolnir_fuzz STATIC ${MJ_SOURCES} src/mj_apply_nocuda.c)
  target_include_directories(mjolnir_fuzz PUBLIC include)
  if(NOT MSVC)
    target_link_libraries(mjolnir_fuzz PUBLIC Threads::Threads m)
  endif()
  if(MSVC)
    target_compile_options(mjolnir_fuzz PRIVATE /fsanitize=fuzzer)
  else()
    target_compile_options(mjolnir_fuzz PRIVATE -fsanitize=fuzzer-no-link)
  endif()
  foreach(ft ledger manifest toy score safet delta)
    add_executable(mj_libfuzz_${ft} tests/fuzz/mj_libfuzzer.c)
    target_link_libraries(mj_libfuzz_${ft} PRIVATE mjolnir_fuzz)
    target_include_directories(mj_libfuzz_${ft} PRIVATE tests/fuzz)
    target_compile_definitions(mj_libfuzz_${ft} PRIVATE MJ_FUZZ_TARGET=mj_fuzz_${ft})
    if(MSVC)
      target_compile_options(mj_libfuzz_${ft} PRIVATE /fsanitize=fuzzer)
    else()
      target_compile_options(mj_libfuzz_${ft} PRIVATE -fsanitize=fuzzer)
      target_link_options(mj_libfuzz_${ft} PRIVATE -fsanitize=fuzzer)
    endif()
  endforeach()
endif()

# The tools, end to end, on a scratch store: a nap, a night, the verifier,
# and the refusals.
set(MJ_TOOL_STORE ${MJ_TEST_TMP}/tool-store-${CMAKE_BUILD_TYPE})
add_test(NAME tool_clean COMMAND ${CMAKE_COMMAND} -E rm -rf ${MJ_TOOL_STORE} ${MJ_TOOL_STORE}.mjl)
add_test(NAME tool_nap COMMAND mj_nap --dry-run --seed-toy --store ${MJ_TOOL_STORE} --ledger ${MJ_TOOL_STORE}.mjl --id ctest-nap)
add_test(NAME tool_nightly COMMAND mj_nightly --dry-run --store ${MJ_TOOL_STORE} --ledger ${MJ_TOOL_STORE}.mjl --id ctest-night)
add_test(NAME tool_ledger_verify COMMAND mj_ledger_verify ${MJ_TOOL_STORE}.mjl)
add_test(NAME tool_refuses_live COMMAND mj_nap --live --dry-run --store ${MJ_TOOL_STORE} --ledger ${MJ_TOOL_STORE}.mjl --id ctest-live)
add_test(NAME tool_refuses_no_dry_run COMMAND mj_nightly --store ${MJ_TOOL_STORE} --ledger ${MJ_TOOL_STORE}.mjl --id ctest-x)
set_tests_properties(tool_refuses_live tool_refuses_no_dry_run PROPERTIES WILL_FAIL TRUE)
set_tests_properties(tool_ledger_verify PROPERTIES PASS_REGULAR_EXPRESSION "OK 2 records")
set_tests_properties(tool_nap PROPERTIES PASS_REGULAR_EXPRESSION "outcome +dry-run-pass")
set_tests_properties(tool_nightly PROPERTIES PASS_REGULAR_EXPRESSION "outcome +dry-run-pass")
set_tests_properties(tool_nap PROPERTIES DEPENDS tool_clean)
set_tests_properties(tool_nightly PROPERTIES DEPENDS tool_nap)
set_tests_properties(tool_ledger_verify PROPERTIES DEPENDS tool_nightly)
set_tests_properties(tool_refuses_live tool_refuses_no_dry_run PROPERTIES DEPENDS tool_ledger_verify)

# MSVC's AddressSanitizer runtime is a DLL beside the compiler; put a copy
# beside the test programs so they start.
if(MSVC AND MJ_SANITIZE)
  get_filename_component(MJ_CL_DIR ${CMAKE_C_COMPILER} DIRECTORY)
  file(GLOB MJ_ASAN_DLLS "${MJ_CL_DIR}/clang_rt.asan_dynamic-x86_64.dll" "${MJ_CL_DIR}/clang_rt.asan_dbg_dynamic-x86_64.dll")
  if(MJ_ASAN_DLLS)
    add_custom_command(TARGET test_hash POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MJ_ASAN_DLLS} $<TARGET_FILE_DIR:test_hash>)
  endif()
endif()

# ---- stage M1 ----
# A read-only checker for any version store, including the real-weight one.
add_executable(mj_store_verify tools/mj_store_verify.c)
target_link_libraries(mj_store_verify PRIVATE mjolnir)
target_compile_options(mj_store_verify PRIVATE ${MJ_WARN})

# The Python side of M1 (labeling tool, replays, GPU toy training, real-weight
# edit path). Its tests also check that the C tools accept what Python writes.
# Point MJ_PYTHON at an interpreter with torch and numpy to run the GPU and
# real-weight tests; without them those tests are skipped, not failed.
option(MJ_PYTHON_TESTS "Run the Python tests for stage M1" ON)
set(MJ_PYTHON "" CACHE FILEPATH "Python interpreter for the M1 tests")
if(MJ_PYTHON_TESTS)
  if(NOT MJ_PYTHON)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
      set(MJ_PYTHON ${Python3_EXECUTABLE})
    endif()
  endif()
  if(MJ_PYTHON)
    foreach(pt ledger store replay labeler toytrain real c_port)
      add_test(NAME py_${pt} COMMAND ${MJ_PYTHON} -m unittest -v tests.test_${pt}
               WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/python)
      set_tests_properties(py_${pt} PROPERTIES
        ENVIRONMENT "MJ_TEST_TMP=${MJ_TEST_TMP};MJ_TOOLS_DIR=$<TARGET_FILE_DIR:mj_nap>"
        TIMEOUT 600)
    endforeach()
  else()
    message(STATUS "Mjolnir: no Python found, the M1 Python tests are not added")
  endif()
endif()
