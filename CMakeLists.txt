cmake_minimum_required(VERSION 4.3)
project(frappe
        VERSION 0.1.0
        DESCRIPTION "Frappe - OOP programming language compiler"
        LANGUAGES C CXX
)
if (WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif ()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif ()

# ============================================================================
# Compiler-family detection
#
# CMAKE_CXX_COMPILER_ID values we care about:
#   MSVC      — cl.exe (pure Microsoft toolchain)
#   Clang     — clang, clang++, clang-cl (LLVM)
#   GNU       — gcc, g++
#
# CMAKE_CXX_SIMULATE_ID is set when a compiler simulates another's CLI:
#   "MSVC"    — clang-cl speaks MSVC-style flags
#   ""        — clang/gcc speak GNU-style flags
#
# Three predicates drive all the branching below:
#   FRAPPE_IS_MSVC_STYLE — MSVC-style CLI (cl.exe OR clang-cl)
#   FRAPPE_IS_CLANG_CL   — clang-cl specifically
#   FRAPPE_IS_GNU_STYLE  — GNU-style CLI (clang, gcc)
# ============================================================================
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(FRAPPE_IS_MSVC_STYLE TRUE)
    set(FRAPPE_IS_CLANG_CL FALSE)
    set(FRAPPE_IS_GNU_STYLE FALSE)
elseif (CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(FRAPPE_IS_MSVC_STYLE TRUE)
    set(FRAPPE_IS_CLANG_CL TRUE)
    set(FRAPPE_IS_GNU_STYLE FALSE)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(FRAPPE_IS_MSVC_STYLE FALSE)
    set(FRAPPE_IS_CLANG_CL FALSE)
    set(FRAPPE_IS_GNU_STYLE TRUE)
else ()
    message(WARNING "Unrecognized compiler: ${CMAKE_CXX_COMPILER_ID} "
            "(SIMULATE_ID=${CMAKE_CXX_SIMULATE_ID}). "
            "Only MSVC, Clang, and GCC are officially supported.")
    set(FRAPPE_IS_MSVC_STYLE FALSE)
    set(FRAPPE_IS_CLANG_CL FALSE)
    set(FRAPPE_IS_GNU_STYLE FALSE)
endif ()

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} "
        "(MSVC-style=${FRAPPE_IS_MSVC_STYLE}, GNU-style=${FRAPPE_IS_GNU_STYLE})")

# ============================================================================
# Options
# ============================================================================
option(FRAPPE_WERROR "Treat warnings as errors" ON)
option(FRAPPE_SANITIZE "Enable sanitizers" OFF)
option(FRAPPE_LTO "LTO in Release" ON)
option(FRAPPE_NATIVE_ARCH "-march=native in Release" OFF)
option(FRAPPE_PCH "Precompiled headers" OFF)
option(FRAPPE_LLVM "LLVM codegen backend" OFF)
option(FRAPPE_BUILD_TESTS "Build the test suite" OFF)

# ============================================================================
# Sanitizers
# ============================================================================
if (FRAPPE_SANITIZE)
    option(FRAPPE_ASAN "  Address sanitizer" ON)
    option(FRAPPE_UBSAN "  Undefined behavior sanitizer" ON)
    option(FRAPPE_TSAN "  Thread sanitizer" OFF)

    if (FRAPPE_IS_MSVC_STYLE AND NOT FRAPPE_IS_CLANG_CL)
        if (FRAPPE_UBSAN OR FRAPPE_TSAN)
            message(FATAL_ERROR
                    "MSVC only supports ASan (FRAPPE_UBSAN and FRAPPE_TSAN must be OFF)")
        endif ()
    elseif (FRAPPE_IS_GNU_STYLE OR FRAPPE_IS_CLANG_CL)
        if (FRAPPE_TSAN AND FRAPPE_ASAN)
            message(FATAL_ERROR
                    "FRAPPE_TSAN and FRAPPE_ASAN cannot both be ON (incompatible)")
        endif ()
    else ()
        message(FATAL_ERROR
                "FRAPPE_SANITIZE requires GCC, Clang, clang-cl, or MSVC")
    endif ()
endif ()

# ============================================================================
# Shared C++ options
# ============================================================================
add_library(frappe_options INTERFACE)

target_include_directories(frappe_options INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(frappe_options INTERFACE
        FRAPPE_VERSION="${PROJECT_VERSION}"
)

# --- warnings ---
# clang-cl delegates -W* flags to the clang frontend, so the full GNU warning
# set works. Pure MSVC uses /W4.
if (FRAPPE_IS_MSVC_STYLE AND NOT FRAPPE_IS_CLANG_CL)
    target_compile_options(frappe_options INTERFACE
            /W4 /permissive- /utf-8)
elseif (FRAPPE_IS_CLANG_CL)
    # clang-cl driver maps -Wall (single dash) to MSVC's /Wall (all
    # warnings, i.e. -Weverything on the -cc1 side), not GNU's -Wall.
    # That enables -Wreserved-macro-identifier etc., which fire on
    # CLion's compiler-info probe file and on MSVC's own headers.
    # Suppress the reserved-identifier warnings so the probe succeeds.
    target_compile_options(frappe_options INTERFACE
            -W4 -Wextra -Wpedantic
            -Wshadow -Wcast-align -Wcast-qual -Wconversion -Wdouble-promotion
            -Wformat=2 -Wimplicit-fallthrough -Wnull-dereference -Wpointer-arith
            -Wredundant-decls -Wswitch-enum -Wundef -Wuninitialized -Wunused
            -Wvla -Wwrite-strings -Wzero-as-null-pointer-constant
            -Wold-style-cast -Woverloaded-virtual -Wnon-virtual-dtor
            -Wctor-dtor-privacy -Wextra-semi
            -Wno-reserved-macro-identifier
            -Wno-reserved-identifier
            -Wno-macro-redefined
            -Wno-language-extension-token)
else ()
    target_compile_options(frappe_options INTERFACE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wcast-align -Wcast-qual -Wconversion -Wdouble-promotion
            -Wformat=2 -Wimplicit-fallthrough -Wnull-dereference -Wpointer-arith
            -Wredundant-decls -Wswitch-enum -Wundef -Wuninitialized -Wunused
            -Wvla -Wwrite-strings -Wzero-as-null-pointer-constant
            -Wold-style-cast -Woverloaded-virtual -Wnon-virtual-dtor
            -Wctor-dtor-privacy -Wextra-semi)
endif ()

# --- warnings-as-errors ---
if (FRAPPE_WERROR)
    if (FRAPPE_IS_MSVC_STYLE AND NOT FRAPPE_IS_CLANG_CL)
        target_compile_options(frappe_options INTERFACE /WX)
    else ()
        target_compile_options(frappe_options INTERFACE -Werror)
    endif ()
endif ()

# --- build-type additions ---
# cl.exe and clang-cl both add /Zi via CMAKE_CXX_FLAGS_DEBUG by default.
# Only add -g3 on GNU-style, where CMake's default is just -g.
if (FRAPPE_IS_GNU_STYLE)
    target_compile_options(frappe_options INTERFACE
            $<$<CONFIG:Debug>:-g3>)
endif ()

# glibc / libstdc++ hardening — no-op on Windows
if (NOT WIN32)
    target_compile_definitions(frappe_options INTERFACE
            $<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>
            $<$<CONFIG:Release>:_FORTIFY_SOURCE=3>)
endif ()

# --- LTO / native arch (Release only) ---
if (FRAPPE_NATIVE_ARCH)
    if (FRAPPE_IS_MSVC_STYLE AND NOT FRAPPE_IS_CLANG_CL)
        target_compile_options(frappe_options INTERFACE
                $<$<CONFIG:Release>:/arch:AVX2>)
    else ()
        target_compile_options(frappe_options INTERFACE
                $<$<CONFIG:Release>:-march=native>)
    endif ()
endif ()
if (FRAPPE_LTO)
    if (FRAPPE_IS_MSVC_STYLE AND NOT FRAPPE_IS_CLANG_CL)
        target_compile_options(frappe_options INTERFACE
                $<$<CONFIG:Release>:/GL>)
        target_link_options(frappe_options INTERFACE
                $<$<CONFIG:Release>:/LTCG>)
    else ()
        target_compile_options(frappe_options INTERFACE
                $<$<CONFIG:Release>:-flto>)
        target_link_options(frappe_options INTERFACE
                $<$<CONFIG:Release>:-flto>)
    endif ()
endif ()

# --- sanitizers ---
if (FRAPPE_SANITIZE)
    if (FRAPPE_IS_MSVC_STYLE AND NOT FRAPPE_IS_CLANG_CL)
        target_compile_options(frappe_options INTERFACE /fsanitize=address)
    else ()
        target_compile_options(frappe_options INTERFACE -fno-omit-frame-pointer -g)
        if (FRAPPE_ASAN)
            target_compile_options(frappe_options INTERFACE -fsanitize=address)
            target_link_options(frappe_options INTERFACE -fsanitize=address)
        endif ()
        if (FRAPPE_UBSAN)
            target_compile_options(frappe_options INTERFACE
                    -fsanitize=undefined -fno-sanitize-recover=undefined)
            target_link_options(frappe_options INTERFACE -fsanitize=undefined)
        endif ()
        if (FRAPPE_TSAN)
            target_compile_options(frappe_options INTERFACE -fsanitize=thread)
            target_link_options(frappe_options INTERFACE -fsanitize=thread)
        endif ()
    endif ()
endif ()

# ============================================================================
# Runtime library (C)
# ============================================================================
add_library(frappe_rt STATIC src/runtime/frappe_rt.c)
set_target_properties(frappe_rt PROPERTIES
        POSITION_INDEPENDENT_CODE ON)
if (NOT WIN32)
    set_target_properties(frappe_rt PROPERTIES C_VISIBILITY_PRESET hidden)
endif ()

# ============================================================================
# Core compiler sources
# ============================================================================
file(GLOB_RECURSE FRAPPE_CORE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/source/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/syntax/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lexer/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ast/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ast_printer/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/parser/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/resolver/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/types/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/checker/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mir/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/fatal/*.cpp"
)

if (NOT FRAPPE_CORE_SOURCES)
    message(FATAL_ERROR
            "No frappe_core sources found — check src/*/*.cpp layout")
endif ()

file(GLOB_RECURSE FRAPPE_CORE_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/frappe/*.hpp"
)

add_library(frappe_core STATIC ${FRAPPE_CORE_SOURCES} ${FRAPPE_CORE_HEADERS})
target_link_libraries(frappe_core PUBLIC frappe_options)

# ============================================================================
# Codegen library
# ============================================================================
file(GLOB_RECURSE FRAPPE_CODEGEN_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/*.cpp"
)

if (NOT FRAPPE_CODEGEN_SOURCES)
    message(FATAL_ERROR "No codegen sources found — expected src/codegen/*.cpp")
endif ()

add_library(frappe_codegen STATIC ${FRAPPE_CODEGEN_SOURCES})
target_link_libraries(frappe_codegen PUBLIC frappe_options)
if (WIN32)
    # Windows paths have backslashes, which break C string literals
    # (e.g. \U is a Unicode escape, \D and \c are invalid). Wrap in a
    # C++ raw string literal R"(...)" so backslashes are literal.
    # On Linux, normal quotes work because the path has forward slashes.
    target_compile_definitions(frappe_codegen PRIVATE
            "FRAPPE_RUNTIME_LIB=R\"($<TARGET_FILE:frappe_rt>)\"")
else ()
    target_compile_definitions(frappe_codegen PRIVATE
            "FRAPPE_RUNTIME_LIB=\"$<TARGET_FILE:frappe_rt>\"")
endif ()
add_dependencies(frappe_codegen frappe_rt)

if (FRAPPE_LLVM)
    if (WIN32)
        if (NOT TARGET ZLIB::ZLIB)
            add_library(ZLIB::ZLIB INTERFACE IMPORTED)
            message(STATUS "Pre-defined ZLIB::ZLIB as empty INTERFACE (LLVM static link)")
        endif ()
        if (NOT TARGET zstd::libzstd_shared)
            add_library(zstd::libzstd_shared INTERFACE IMPORTED)
            message(STATUS "Pre-defined zstd::libzstd_shared as empty INTERFACE")
        endif ()
        if (NOT TARGET zstd::libzstd_static)
            add_library(zstd::libzstd_static INTERFACE IMPORTED)
        endif ()
    endif ()

    find_package(LLVM REQUIRED CONFIG)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

    target_include_directories(frappe_codegen SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(frappe_codegen PRIVATE FRAPPE_LLVM_ENABLED)

    if (NOT LLVM_ENABLE_RTTI)
        message(STATUS "LLVM built without RTTI — disabling RTTI on codegen TUs")
        if (FRAPPE_IS_CLANG_CL)
            target_compile_options(frappe_codegen PRIVATE /clang:-fno-rtti)
        elseif (FRAPPE_IS_MSVC_STYLE)
            target_compile_options(frappe_codegen PRIVATE /GR-)
        else ()
            target_compile_options(frappe_codegen PRIVATE -fno-rtti)
        endif ()
    endif ()
    if (NOT LLVM_ENABLE_EH)
        message(STATUS "LLVM built without exceptions — disabling EH on codegen TUs")
        if (FRAPPE_IS_CLANG_CL)
            target_compile_options(frappe_codegen PRIVATE /clang:-fno-exceptions)
        elseif (FRAPPE_IS_MSVC_STYLE)
            target_compile_options(frappe_codegen PRIVATE /EHs-c-)
        else ()
            target_compile_options(frappe_codegen PRIVATE -fno-exceptions)
        endif ()
    endif ()

    if (LLVM_LINK_LLVM_DYLIB)
        message(STATUS "Linking monolithic libLLVM")
        target_link_libraries(frappe_codegen PRIVATE LLVM)
    else ()
        # LLVM_NATIVE_ARCH isn't set for consumers of the prebuilt Windows
        # package, so llvm_map_components_to_libnames('Native') emits
        # LLVMNative.lib verbatim (which doesn't exist). Use explicit X86
        # libs on Windows; Native works on Linux where LLVMConfig.cmake
        # sets LLVM_NATIVE_ARCH.
        if (WIN32)
            llvm_map_components_to_libnames(FRAPPE_LLVM_LIBS
                    Core Support Analysis Passes Target MC
                    X86AsmParser X86CodeGen X86Desc X86Disassembler X86Info)
        else ()
            llvm_map_components_to_libnames(FRAPPE_LLVM_LIBS
                    Core Support Analysis Passes Target MC Native)
        endif ()
        target_link_libraries(frappe_codegen PRIVATE ${FRAPPE_LLVM_LIBS})
    endif ()
endif ()

# ============================================================================
# Compiler executable
# ============================================================================
add_executable(frappe src/main.cpp)
target_link_libraries(frappe PRIVATE frappe_core frappe_codegen)

if (FRAPPE_LLVM)
    target_compile_definitions(frappe PRIVATE FRAPPE_LLVM_ENABLED)
endif ()

# ============================================================================
# Precompiled headers
# ============================================================================
if (FRAPPE_PCH)
    set(FRAPPE_PCH_HEADERS
            <cstddef> <cstdint> <memory_resource>
            <string> <format> <span> <string_view> <vector>)
    target_precompile_headers(frappe_core PRIVATE ${FRAPPE_PCH_HEADERS})
    target_precompile_headers(frappe_codegen PRIVATE ${FRAPPE_PCH_HEADERS})
    target_precompile_headers(frappe PRIVATE ${FRAPPE_PCH_HEADERS})
endif ()

# ============================================================================
# Tests
# ============================================================================
if (FRAPPE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()