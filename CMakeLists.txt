cmake_minimum_required(VERSION 3.10)

# Set the project name back to C
project(sql_indexer C)

# Add the executable with main.c and sql_indexer.c
add_executable(sql_indexer main.c sql_indexer.c)

# Optional: Specify C standard if needed
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

add_compile_options(-Wall -Wextra -Werror -pedantic)

find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIR})
target_link_libraries(sql_indexer ${CURSES_LIBRARIES})