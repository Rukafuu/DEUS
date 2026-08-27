cmake_minimum_required(VERSION 3.20)
project(DEUS VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(DEUS_BUILD_BENCHMARKS "Build local DEUS regression benchmarks" OFF)

if(MSVC)
  add_compile_options(/W4 /WX /O2 /guard:cf)
  add_link_options(/DYNAMICBASE /NXCOMPAT)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror -O3)
endif()

add_library(deuscore STATIC
  src/deus_format.c
  src/deus_json.c
  src/deus_project.c
  src/deus_formatter.c
  src/deus_lexer.c
  src/deus_validate.c
  src/deus_expression.c
  src/deus_layout.c
  src/deus_ast.c
  src/deus_parser.c
  src/deus_semantic.c
  src/deus_source_ast.c
  src/deus_source_parser.c
  src/deus_source_lower.c
  src/deus_source.c
  src/deus_host.c
  src/deus_execution.c
  src/deus_value.c)
target_include_directories(deuscore PUBLIC include)
target_include_directories(deuscore PRIVATE src)

add_executable(deusc src/deusc.c)
target_link_libraries(deusc PRIVATE deuscore)
add_library(deusvmportable STATIC src/deusvm_unavailable.c)
target_include_directories(deusvmportable PRIVATE src)
target_link_libraries(deusvmportable PRIVATE deuscore)

if(WIN32)
  add_library(deusvmcore STATIC src/deusvm.c)
  target_compile_definitions(deusvmcore PRIVATE DEUS_VM_NO_MAIN)
  target_include_directories(deusvmcore PRIVATE src)
  target_link_libraries(deusvmcore PRIVATE deuscore)

  add_executable(deusvm src/deusvm.c)
  target_include_directories(deusvm PRIVATE src)
  target_link_libraries(deusvm PRIVATE deuscore)
else()
  add_library(deusvmcore ALIAS deusvmportable)
  add_executable(deusvm src/deusvm_portable_main.c)
  target_link_libraries(deusvm PRIVATE deuscore deusvmportable)
endif()

add_executable(deus src/deus_cli.c)
target_link_libraries(deus PRIVATE deuscore deusvmcore)

add_executable(deus-language-server src/deus_lsp.c)
target_link_libraries(deus-language-server PRIVATE deuscore)

install(TARGETS deus deusc deusvm deus-language-server RUNTIME DESTINATION bin)
install(FILES assets/deus-logo.png assets/deus-logo.ico DESTINATION share/deus)

if(DEUS_BUILD_BENCHMARKS AND WIN32)
  add_executable(deus-bench bench/deus_bench.c)
  target_link_libraries(deus-bench PRIVATE deuscore deusvmcore)
  target_link_libraries(deus-bench PRIVATE psapi)
elseif(DEUS_BUILD_BENCHMARKS)
  message(WARNING "DEUS benchmarks currently require the Windows reference VM")
endif()

if(WIN32)
  target_sources(deus PRIVATE src/deus.rc)
  target_sources(deusc PRIVATE src/deus.rc)
  target_sources(deusvm PRIVATE src/deus.rc)
  target_compile_definitions(deusvmcore PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
  target_link_libraries(deusvmcore PRIVATE winhttp)
  target_compile_definitions(deus PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
  target_link_libraries(deus PRIVATE winhttp)
  target_compile_definitions(deusvm PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
  target_link_libraries(deusvm PRIVATE winhttp)
else()
  message(STATUS "Building portable DEUS tooling without the WinHTTP reference runtime")
endif()

enable_testing()
add_executable(deus_semantic_bytecode_tests tests/test_semantic_bytecode.c)
target_link_libraries(deus_semantic_bytecode_tests PRIVATE deuscore)
add_test(NAME deus_semantic_bytecode COMMAND deus_semantic_bytecode_tests)

add_executable(deus_portable_vm_tests tests/test_portable_vm.c)
target_link_libraries(deus_portable_vm_tests PRIVATE deuscore deusvmportable)
target_compile_definitions(deus_portable_vm_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
add_test(NAME deus_portable_vm COMMAND deus_portable_vm_tests)

if(WIN32)
  add_test(NAME deus_lsp_protocol
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
      -File ${CMAKE_SOURCE_DIR}/scripts/test-lsp.ps1
      -Server $<TARGET_FILE:deus-language-server>)
endif()
add_executable(deus_host_call_tests tests/test_host_call.c)
target_link_libraries(deus_host_call_tests PRIVATE deuscore deusvmportable)
target_compile_definitions(deus_host_call_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
add_test(NAME deus_host_call COMMAND deus_host_call_tests)
add_executable(deus_tests tests/test_deus.c)
target_link_libraries(deus_tests PRIVATE deuscore)
add_test(NAME deus_format_roundtrip COMMAND deus_tests)

add_executable(deus_malformed_tests tests/test_malformed.c)
target_link_libraries(deus_malformed_tests PRIVATE deuscore)
add_test(NAME deus_hostile_inputs COMMAND deus_malformed_tests)

add_executable(deus_value_tests tests/test_values.c)
target_link_libraries(deus_value_tests PRIVATE deuscore)
add_test(NAME deus_values COMMAND deus_value_tests)

add_executable(deus_compiler_tests tests/test_compiler.c)
target_include_directories(deus_compiler_tests PRIVATE src)
target_link_libraries(deus_compiler_tests PRIVATE deuscore)
add_test(NAME deus_compiler_pipeline COMMAND deus_compiler_tests)

add_executable(deus_json_tests tests/test_json.c)
target_include_directories(deus_json_tests PRIVATE src)
target_link_libraries(deus_json_tests PRIVATE deuscore)
add_test(NAME deus_json_scalars COMMAND deus_json_tests)

add_executable(deus_project_tests tests/test_project.c)
target_include_directories(deus_project_tests PRIVATE src)
target_link_libraries(deus_project_tests PRIVATE deuscore)
add_test(NAME deus_project_manifest COMMAND deus_project_tests)

add_executable(deus_limits_valid_tests tests/test_limits_valid.c)
target_include_directories(deus_limits_valid_tests PRIVATE src)
target_link_libraries(deus_limits_valid_tests PRIVATE deuscore)
add_test(NAME deus_limits_valid COMMAND deus_limits_valid_tests)

add_executable(deus_limits_adversarial_tests tests/test_limits_adversarial.c)
target_include_directories(deus_limits_adversarial_tests PRIVATE src)
target_link_libraries(deus_limits_adversarial_tests PRIVATE deuscore)
add_test(NAME deus_limits_adversarial COMMAND deus_limits_adversarial_tests)

add_executable(deus_formatter_tests tests/test_formatter.c)
target_include_directories(deus_formatter_tests PRIVATE src)
target_link_libraries(deus_formatter_tests PRIVATE deuscore)
add_test(NAME deus_formatter_roundtrip COMMAND deus_formatter_tests)

add_executable(deus_limits_compatibility_tests tests/test_limits_compatibility.c)
target_include_directories(deus_limits_compatibility_tests PRIVATE src)
target_link_libraries(deus_limits_compatibility_tests PRIVATE deuscore)
add_test(NAME deus_limits_compatibility COMMAND deus_limits_compatibility_tests)

add_executable(deus_host_abi_tests tests/test_host_abi.c)
target_link_libraries(deus_host_abi_tests PRIVATE deuscore)
add_test(NAME deus_host_abi COMMAND deus_host_abi_tests)

add_executable(deus_output_sink_tests tests/test_output_sink.c)
target_link_libraries(deus_output_sink_tests PRIVATE deuscore deusvmportable)
add_test(NAME deus_output_sink COMMAND deus_output_sink_tests)

add_executable(deus_execution_controls_tests tests/test_execution_controls.c)
target_link_libraries(deus_execution_controls_tests PRIVATE deuscore deusvmportable)
add_test(NAME deus_execution_controls COMMAND deus_execution_controls_tests)

if(WIN32)
  add_executable(deus_execution_controls_native_tests tests/test_execution_controls.c)
  target_link_libraries(deus_execution_controls_native_tests PRIVATE deuscore deusvmcore)
  add_test(NAME deus_execution_controls_native COMMAND deus_execution_controls_native_tests)

  add_executable(deus_async_tests tests/test_async.c)
  target_link_libraries(deus_async_tests PRIVATE deuscore deusvmcore ws2_32)
  add_test(NAME deus_async_local COMMAND deus_async_tests)
  add_test(NAME deus_cli_check COMMAND deus check ${CMAKE_SOURCE_DIR}/examples/structured_result.deus)
  add_test(NAME deus_cli_run COMMAND deus run ${CMAKE_SOURCE_DIR}/examples/structured_result.deus)
  add_test(NAME deus_cli_flow COMMAND deus run ${CMAKE_SOURCE_DIR}/examples/indented_flow.deus)
endif()
