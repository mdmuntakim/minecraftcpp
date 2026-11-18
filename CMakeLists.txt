cmake_minimum_required(VERSION 3.20)
project(MinecraftCpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(MCC_WARNINGS -Wall -Wextra)
add_library(mcc_core STATIC
    src/core/block.cpp
    src/core/game.cpp
    src/core/mesher.cpp
    src/core/player.cpp
    src/core/texture.cpp
    src/core/world.cpp
)
target_include_directories(mcc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(mcc_core PRIVATE ${MCC_WARNINGS})

if(ANDROID)
    add_library(native_app_glue STATIC
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
    target_include_directories(native_app_glue PUBLIC
        ${ANDROID_NDK}/sources/android/native_app_glue)

    add_library(minecraftcpp SHARED
        src/android/main.cpp
        src/android/renderer.cpp
    )
    target_include_directories(minecraftcpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(minecraftcpp PRIVATE ${MCC_WARNINGS})
    target_link_libraries(minecraftcpp PRIVATE
        mcc_core
        native_app_glue
        EGL
        GLESv3
        android
        log
        m
    )
    # NativeActivity entry point lives in android_native_app_glue; it has to be
    # pulled in even though nothing references it from C++.
    target_link_options(minecraftcpp PRIVATE -u ANativeActivity_onCreate)
    set_target_properties(minecraftcpp PROPERTIES OUTPUT_NAME "minecraftcpp")
else()
    enable_testing()

    add_executable(mcc_tests src/host/tests.cpp)
    target_link_libraries(mcc_tests PRIVATE mcc_core)

    add_executable(mcc_bench src/host/bench.cpp)
    target_link_libraries(mcc_bench PRIVATE mcc_core)

    add_executable(mcc_icongen tools/icongen.cpp)
    target_link_libraries(mcc_icongen PRIVATE mcc_core)
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        target_link_libraries(mcc_icongen PRIVATE ZLIB::ZLIB)
        target_compile_definitions(mcc_icongen PRIVATE MCC_HAVE_ZLIB=1)
    endif()

    add_test(NAME core COMMAND mcc_tests)
    add_test(NAME bench-smoke COMMAND mcc_bench)
    set_tests_properties(bench-smoke PROPERTIES LABELS "bench")
endif()
