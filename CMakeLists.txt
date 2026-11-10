cmake_minimum_required(VERSION 3.16)
project(nokia3310 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

set(CORE_SRCS
    src/s20_assets.c
    src/s20_audio.c
    src/s20_games.c
    src/s20_games_data.c
    src/s20_lcd.c
    src/s20_menu.c
)

add_library(s20_core STATIC ${CORE_SRCS})
target_include_directories(s20_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(s20_core PRIVATE /W3 /utf-8)
else()
    target_compile_options(s20_core PRIVATE -Wall -Wextra)
endif()

if(NOT WIN32)
    target_link_libraries(s20_core PUBLIC m)
endif()

# Locate SDL2 across Linux, macOS (Homebrew/Framework), and Windows (vcpkg/MSYS2/SDK)
find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
    find_package(SDL2 QUIET)
endif()
if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        set(S20_SDL2_TARGET PkgConfig::SDL2)
    else()
        message(FATAL_ERROR "SDL2 not found. Pass -DCMAKE_PREFIX_PATH=<path-to-SDL2> or install libsdl2-dev / brew install sdl2 / vcpkg sdl2.")
    endif()
else()
    if(TARGET SDL2::SDL2)
        set(S20_SDL2_TARGET SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
        set(S20_SDL2_TARGET SDL2::SDL2-static)
    else()
        set(S20_SDL2_TARGET ${SDL2_LIBRARIES})
    endif()
endif()

add_executable(nokia3310 src/platform_sdl2.c src/s20_phone_skin.c)
target_link_libraries(nokia3310 PRIVATE s20_core ${S20_SDL2_TARGET})
if(NOT TARGET SDL2::SDL2 AND NOT TARGET PkgConfig::SDL2 AND DEFINED SDL2_INCLUDE_DIRS)
    target_include_directories(nokia3310 PRIVATE ${SDL2_INCLUDE_DIRS})
endif()
if(MSVC)
    target_compile_options(nokia3310 PRIVATE /W3 /utf-8)
else()
    target_compile_options(nokia3310 PRIVATE -Wall -Wextra)
endif()

if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    add_custom_command(TARGET nokia3310 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:nokia3310>
            $<TARGET_FILE_DIR:nokia3310>
        COMMAND_EXPAND_LISTS
    )
endif()

add_executable(smoke_test tests/smoke_test.c)
target_link_libraries(smoke_test PRIVATE s20_core)
if(MSVC)
    target_compile_options(smoke_test PRIVATE /W3 /utf-8)
else()
    target_compile_options(smoke_test PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME smoke_test COMMAND smoke_test)
