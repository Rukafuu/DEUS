cmake_minimum_required(VERSION 3.20)
project(DEUS VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /WX /O2 /guard:cf)
  add_link_options(/DYNAMICBASE /NXCOMPAT)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror -O3)
endif()

add_library(deuscore STATIC
  src/deus_format.c
  src/deus_json.c
  src/deus_lexer.c
  src/deus_expression.c
  src/deus_layout.c
  src/deus_ast.c
  src/deus_parser.c
  src/deus_semantic.c
  src/deus_source.c
  src/deus_value.c)
target_include_directories(deuscore PUBLIC include)
target_include_directories(deuscore PRIVATE src)

add_executable(deusc src/deusc.c)
target_link_libraries(deusc PRIVATE deuscore)

add_library(deusvmcore STATIC src/deusvm.c)
target_compile_definitions(deusvmcore PRIVATE DEUS_VM_NO_MAIN)
target_include_directories(deusvmcore PRIVATE src)
target_link_libraries(deusvmcore PRIVATE deuscore)

add_executable(deus src/deus_cli.c)
target_link_libraries(deus PRIVATE deuscore deusvmcore)

add_executable(deusvm src/deusvm.c)
target_include_directories(deusvm PRIVATE src)
target_link_libraries(deusvm PRIVATE deuscore)

install(TARGETS deus deusc deusvm RUNTIME DESTINATION bin)
install(FILES assets/deus-logo.png assets/deus-logo.ico DESTINATION share/deus)

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
  message(FATAL_ERROR "DEUS VM v1 uses native WinHTTP and currently targets Windows")
endif()

enable_testing()
add_executable(deus_tests tests/test_deus.c)
target_link_libraries(deus_tests PRIVATE deuscore)
add_test(NAME deus_format_roundtrip COMMAND deus_tests)

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

if(WIN32)
  add_executable(deus_async_tests tests/test_async.c)
  target_link_libraries(deus_async_tests PRIVATE deuscore deusvmcore ws2_32)
  add_test(NAME deus_async_local COMMAND deus_async_tests)
  add_test(NAME deus_cli_check COMMAND deus check ${CMAKE_SOURCE_DIR}/examples/structured_result.deus)
  add_test(NAME deus_cli_run COMMAND deus run ${CMAKE_SOURCE_DIR}/examples/structured_result.deus)
  add_test(NAME deus_cli_flow COMMAND deus run ${CMAKE_SOURCE_DIR}/examples/indented_flow.deus)
endif()
