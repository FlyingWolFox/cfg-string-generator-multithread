cmake_minimum_required(VERSION 3.0.0)
project(cfg_string_generator VERSION 0.1.0)

include(CTest)
enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(cfg_string_generator main.cpp)
target_compile_definitions(cfg_string_generator PRIVATE FLAGS=0 DERIVATION_ENABLE=1)
target_link_libraries(${PROJECT_NAME} Threads::Threads)

if(MSVC)
  target_compile_options(cfg_string_generator PRIVATE /W4 /WX)
else()
  target_compile_options(cfg_string_generator PRIVATE -Wall -Wextra -pedantic -Wconversion -Woverloaded-virtual)
endif()

foreach(FLAGS_I RANGE 7)
  add_executable("cfg_STRINGS_${FLAGS_I}" main.cpp)
  target_compile_definitions("cfg_STRINGS_${FLAGS_I}" PRIVATE FLAGS=${FLAGS_I} DERIVATION_ENABLE=0)
  target_compile_options("cfg_STRINGS_${FLAGS_I}" PRIVATE -Wfatal-errors)
  target_link_libraries("cfg_STRINGS_${FLAGS_I}" Threads::Threads)
endforeach()

foreach(FLAGS_I RANGE 31)
  add_executable("cfg_DERIVATIONS_${FLAGS_I}" main.cpp)
  target_compile_definitions("cfg_DERIVATIONS_${FLAGS_I}" PRIVATE FLAGS=${FLAGS_I} DERIVATION_ENABLE=1)
  target_compile_options("cfg_DERIVATIONS_${FLAGS_I}" PRIVATE -Wfatal-errors)
  target_link_libraries("cfg_DERIVATIONS_${FLAGS_I}" Threads::Threads)
endforeach()


set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
