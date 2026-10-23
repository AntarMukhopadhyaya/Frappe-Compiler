cmake_minimum_required(VERSION 4.3)
project(frappe
        VERSION 0.1.0
        DESCRIPTION "Frappe - OOP programming language compiler"
        LANGUAGES C CXX
)

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
# Options
# ============================================================================
option(FRAPPE_WERROR "Treat warnings as errors" ON)
option(FRAPPE_SANITIZE "Enable sanitizers" OFF)
option(FRAPPE_LTO "LTO in Release" ON)
option(FRAPPE_NATIVE_ARCH "-march=native in Release" OFF)
option(FRAPPE_PCH "Precompiled headers" ON)
option(FRAPPE_LLVM "LLVM codegen backend" OFF)
option(FRAPPE_BUILD_TESTS "Build the test suite" OFF)

# ============================================================================
# Sanitizers — sub-options exist only when FRAPPE_SANITIZE is on
# ============================================================================
if (FRAPPE_SANITIZE)
    option(FRAPPE_ASAN "  Address sanitizer" ON)
    option(FRAPPE_UBSAN "  Undefined behavior sanitizer" ON)
    option(FRAPPE_TSAN "  Thread sanitizer" OFF)

    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR
                "FRAPPE_SANITIZE currently requires GCC or Clang "
                "(detected: ${CMAKE_CXX_COMPILER_ID})")
    endif ()
    if (FRAPPE_TSAN AND FRAPPE_ASAN)
        message(FATAL_ERROR
                "FRAPPE_TSAN and FRAPPE_ASAN cannot both be ON (incompatible)")
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

# --- warnings: gate to GCC / Clang ---
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(frappe_options INTERFACE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wcast-align -Wcast-qual -Wconversion -Wdouble-promotion
            -Wformat=2 -Wimplicit-fallthrough -Wnull-dereference -Wpointer-arith
            -Wredundant-decls -Wswitch-enum -Wundef -Wuninitialized -Wunused
            -Wvla -Wwrite-strings -Wzero-as-null-pointer-constant
            -Wold-style-cast -Woverloaded-virtual -Wnon-virtual-dtor
            -Wctor-dtor-privacy -Wextra-semi
    )
endif ()

# --- warnings-as-errors: compiler-specific spelling ---
if (FRAPPE_WERROR)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(frappe_options INTERFACE /WX)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(frappe_options INTERFACE -Werror)
    endif ()
endif ()

# --- build-type additions via generator expressions ---
#
# We do NOT set -O0/-O2/-O3/-Os or -DNDEBUG explicitly. CMake's default
# CMAKE_CXX_FLAGS_<CONFIG> already provides the right values, and letting
# CMake own them means a user can still override with
#     -DCMAKE_CXX_FLAGS_RELEASE=-O2
# without us fighting them. We only add things CMake doesn't.
#
target_compile_options(frappe_options INTERFACE
        $<$<CONFIG:Debug>:-g3>          # more verbose than CMake's default -g
)
target_compile_definitions(frappe_options INTERFACE
        $<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>
        $<$<CONFIG:Release>:_FORTIFY_SOURCE=3>
)

# --- LTO / native arch (Release only, GCC/Clang only) ---
if (FRAPPE_NATIVE_ARCH AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(frappe_options INTERFACE
            $<$<CONFIG:Release>:-march=native>)
endif ()
if (FRAPPE_LTO AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(frappe_options INTERFACE
            $<$<CONFIG:Release>:-flto>)
    target_link_options(frappe_options INTERFACE
            $<$<CONFIG:Release>:-flto>)
endif ()

# --- sanitizers (GCC/Clang only; guarded above) ---
if (FRAPPE_SANITIZE)
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

# ============================================================================
# Runtime library (C) — linked into compiled user programs, not into frappe
# ============================================================================
add_library(frappe_rt STATIC src/runtime/frappe_rt.c)
set_target_properties(frappe_rt PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
)

# ============================================================================
# Core compiler sources — codegen intentionally excluded (built separately)
# ============================================================================
file(GLOB_RECURSE FRAPPE_CORE_SOURCES CONFIGURE_DEPENDS
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
# Codegen library — separate target so its flags stay isolated
# ============================================================================
file(GLOB_RECURSE FRAPPE_CODEGEN_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/*.cpp"
)

if (NOT FRAPPE_CODEGEN_SOURCES)
    message(FATAL_ERROR "No codegen sources found — expected src/codegen/*.cpp")
endif ()

add_library(frappe_codegen STATIC ${FRAPPE_CODEGEN_SOURCES})
target_link_libraries(frappe_codegen PUBLIC frappe_options)
target_compile_definitions(frappe_codegen PRIVATE
        FRAPPE_RUNTIME_LIB="$<TARGET_FILE:frappe_rt>")
add_dependencies(frappe_codegen frappe_rt)

if (FRAPPE_LLVM)
    find_package(LLVM REQUIRED CONFIG)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

    # LLVM include dirs + ABI flags apply only to the codegen translation
    # units, not to the whole compiler.
    target_include_directories(frappe_codegen SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(frappe_codegen PRIVATE FRAPPE_LLVM_ENABLED)

    if (NOT LLVM_ENABLE_RTTI)
        message(STATUS "LLVM built without RTTI — adding -fno-rtti to codegen TUs")
        target_compile_options(frappe_codegen PRIVATE -fno-rtti)
    endif ()
    if (NOT LLVM_ENABLE_EH)
        message(STATUS "LLVM built without exceptions — adding -fno-exceptions to codegen TUs")
        target_compile_options(frappe_codegen PRIVATE -fno-exceptions)
    endif ()

    # Arch/Fedora and other distros ship a monolithic libLLVM.so with no
    # static component archives. LLVM_LINK_LLVM_DYLIB is set by find_package
    # when that's the case, so link the imported `LLVM` target directly.
    # Otherwise fall back to the component libraries.
    if (LLVM_LINK_LLVM_DYLIB)
        message(STATUS "Linking monolithic libLLVM")
        target_link_libraries(frappe_codegen PRIVATE LLVM)
    else ()
        llvm_map_components_to_libnames(FRAPPE_LLVM_LIBS
                Core        # IRBuilder, Module, Type, Function, StructType, BasicBlock
                Support     # Error, StringRef, raw_ostream, Triple
                Analysis      # verifyModule
                Passes
                Target          # Target, TargetMachine base classes
                MC              # MCContext, MCRegisterInfo
                Native

        )
        target_link_libraries(frappe_codegen PRIVATE ${FRAPPE_LLVM_LIBS})
    endif ()
endif ()

# ============================================================================
# Compiler executable
# ============================================================================
add_executable(frappe src/main.cpp)
target_link_libraries(frappe PRIVATE frappe_core frappe_codegen)

# The public codegen API is LLVM-free (string in, string out), so main.cpp
# never includes <llvm/...> and needs no LLVM include dirs, RTTI or exception
# flags. The libLLVM link requirement already propagates from frappe_codegen
# via PRIVATE static-library link semantics. Only the define is useful here
# for feature-flagging in main.cpp.
if (FRAPPE_LLVM)
    target_compile_definitions(frappe PRIVATE FRAPPE_LLVM_ENABLED)
endif ()

# ============================================================================
# Precompiled headers — small STL subset, three targets
# ============================================================================
if (FRAPPE_PCH)
    set(FRAPPE_PCH_HEADERS
            <cstddef> <cstdint> <memory_resource>
            <string> <format> <span> <string_view> <vector>
    )
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