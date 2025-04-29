cmake_minimum_required(VERSION 3.10)

# Set the project name back to C
project(sql_indexer C)

# Add the executable with main.c and sql_indexer.c
# Find ncurses library
find_package(Curses REQUIRED)

add_executable(sql_indexer
    main.c
    sql_indexer.c
    sql_utils.c
    sql_cli.c
)

target_include_directories(sql_indexer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sql_indexer ${CURSES_LIBRARIES})

# Optional: Specify C standard if needed
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)