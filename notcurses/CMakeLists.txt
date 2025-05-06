cmake_minimum_required(VERSION 3.10)
project(notcurses_selector)

set(CMAKE_C_STANDARD 11)

    include(FetchContent)
    FetchContent_Declare(
        notcurses
        GIT_REPOSITORY https://github.com/dankamongmen/notcurses.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(notcurses)

    add_executable(string_selector string_selector.c)
    target_link_libraries(string_selector PRIVATE notcurses)
endif()