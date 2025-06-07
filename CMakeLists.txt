cmake_minimum_required(VERSION 3.31)
project(Ufficio_Postale_Giugno2025 C)

set(CMAKE_C_STANDARD 11)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../build)

add_executable(manager src/manager.c lib/utils.c lib/sem_handling.c )