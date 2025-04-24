cmake_minimum_required(VERSION 3.10)

# Set the project name
project(sql_indexer C)

# Add the executable
add_executable(sql_indexer sql_indexer.c)

# Optional: Specify C standard if needed
# set(CMAKE_C_STANDARD 11)
# set(CMAKE_C_STANDARD_REQUIRED True)