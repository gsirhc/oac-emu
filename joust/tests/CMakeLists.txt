cmake_minimum_required(VERSION 3.14)
project(joust_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/googletest.cmake)

enable_testing()

option(COVERAGE "instrument the unit-test binaries for llvm-cov" OFF)

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CPU ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/cpu)
set(SHARED ${CMAKE_CURRENT_SOURCE_DIR}/../../shared)
set(WILLIAMS ${SHARED}/williams)

function(add_unit_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ${CORE} ${CPU} ${SHARED} ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(${name} PRIVATE GTest::gtest_main)
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  target_compile_definitions(${name} PRIVATE JOUST_RIDDLE_DIR="${CORE}/riddle")
  if(COVERAGE)
    target_compile_options(${name} PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(${name} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  endif()
endfunction()

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hwtest_roms.h
  COMMAND python3 ${CORE}/roms/hwtest/gen_hwtest.py
          --dir ${CMAKE_CURRENT_BINARY_DIR}/hwtest
          --header ${CMAKE_CURRENT_BINARY_DIR}/hwtest_roms.h
  DEPENDS ${CORE}/roms/hwtest/gen_hwtest.py ${SHARED}/hwtest_font.py
  COMMENT "generate Joust hardware self-test ROM"
)
add_custom_target(hwtest_roms DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hwtest_roms.h)

set(CHIPS
  ${WILLIAMS}/pia6821.cpp ${WILLIAMS}/blitter.cpp ${WILLIAMS}/sound_board.cpp
  ${CPU}/cpu_m6809.cpp ${CPU}/cpu_m6800.cpp)
set(BOARD ${CORE}/machine.cpp ${CHIPS})

add_unit_test(williams_test
  ${WILLIAMS}/tests/pia6821_test.cpp ${WILLIAMS}/tests/blitter_test.cpp
  ${WILLIAMS}/tests/sound_board_test.cpp ${CHIPS})
add_unit_test(machine_test machine_test.cpp ${BOARD})
add_dependencies(machine_test hwtest_roms)
add_unit_test(smoke_test smoke_test.cpp ${BOARD})
add_dependencies(smoke_test hwtest_roms)

include(GoogleTest)
gtest_discover_tests(williams_test)
gtest_discover_tests(machine_test)
gtest_discover_tests(smoke_test)
