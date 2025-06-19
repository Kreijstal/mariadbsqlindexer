cmake_minimum_required(VERSION 3.10)

# Set the project name back to C
project(sql_indexer C)

# Add the executable with main.c and sql_indexer.c
add_executable(sql_indexer main.c sql_indexer.c sha256.c)

# Optional: Specify C standard if needed
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

add_compile_options(-Wall -Wextra -Werror -pedantic)

find_package(Curses REQUIRED)
find_package(cJSON REQUIRED)

include_directories(${CURSES_INCLUDE_DIR})

target_link_libraries(sql_indexer PRIVATE ${CURSES_LIBRARIES} cjson)