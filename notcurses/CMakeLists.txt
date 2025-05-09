cmake_minimum_required(VERSION 3.10)
project(notcurses_selector)

set(CMAKE_BUILD_TYPE Debug) # Add this line for debug symbols
set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NOTCURSES QUIET notcurses)

if(NOT NOTCURSES_FOUND)
    message(STATUS "notcurses not found in system, fetching from GitHub...")
    include(FetchContent)
    FetchContent_Declare(
        notcurses
        GIT_REPOSITORY https://github.com/dankamongmen/notcurses.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(notcurses)
endif()

add_executable(string_selector string_selector.c)

if(NOTCURSES_FOUND)
    target_include_directories(string_selector PRIVATE ${NOTCURSES_INCLUDE_DIRS})
    target_link_libraries(string_selector PRIVATE ${NOTCURSES_LIBRARIES})
else()
    target_link_libraries(string_selector PRIVATE notcurses)
endif()