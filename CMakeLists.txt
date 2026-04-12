cmake_minimum_required(VERSION 3.30)
project(greeter)

set(CMAKE_CXX_STANDARD 17)

find_package(Curses REQUIRED)

# Source files
set(SOURCES
        src/main.cpp
        src/ui/menu.cpp
        src/auth/pam_auth.cpp
        src/session/session_manager.cpp
)

add_executable(greeter ${SOURCES})

target_include_directories(greeter PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CURSES_INCLUDE_DIRS}
)

target_link_libraries(greeter
        ${CURSES_LIBRARIES}
        pam
)