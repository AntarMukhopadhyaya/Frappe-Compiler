# ============================================================================
# Frappe test suite — Catch2 v3 via FetchContent.
#
# Processed only when FRAPPE_BUILD_TESTS=ON (see root CMakeLists.txt).
# The compiler build has zero dependency on this file.
#
# Requires from root:
#   option(FRAPPE_BUILD_TESTS ... OFF)
#   if(FRAPPE_BUILD_TESTS)
#       enable_testing()
#       add_subdirectory(tests)   ← plural
#   endif()
# ============================================================================

# ---------------------------------------------------------------------------
# 1. Fetch Catch2
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE
        SYSTEM
)
FetchContent_MakeAvailable(Catch2)

# ---------------------------------------------------------------------------
# 2. Discover test sources — MUST run before add_executable
# ---------------------------------------------------------------------------
file(GLOB_RECURSE FRAPPE_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/regression/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/helpers/*.cpp"
)

if(NOT FRAPPE_TEST_SOURCES)
    message(WARNING
            "FRAPPE_BUILD_TESTS=ON but no test sources found. "
            "Create at least one .cpp under tests/{unit,integration,regression,helpers}/.")
    return()
endif()

# ---------------------------------------------------------------------------
# 3. Test executable
# ---------------------------------------------------------------------------
add_executable(frappe_tests ${FRAPPE_TEST_SOURCES})

target_link_libraries(frappe_tests PRIVATE
        frappe_core
        Catch2::Catch2WithMain
)
target_link_libraries(frappe_tests PRIVATE frappe_codegen)

if (FRAPPE_LLVM)
    target_compile_definitions(frappe_tests PRIVATE FRAPPE_LLVM_ENABLED)
endif ()

# ---------------------------------------------------------------------------
# 4. Include paths
#
# ${catch2_SOURCE_DIR}/src is explicit (not just inherited from Catch2::Catch2)
# so clangd always resolves <catch2/...> in compile_commands.json, even if the
# Catch2 target's INTERFACE include propagation is slow to regenerate.
# ---------------------------------------------------------------------------
target_include_directories(frappe_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${catch2_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# 5. Compile definitions
# ---------------------------------------------------------------------------
target_compile_definitions(frappe_tests PRIVATE
        FRAPPE_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
)

# ---------------------------------------------------------------------------
# 6. Relax warnings that fire constantly in test code but never in compiler
#    code. The compiler library stays strict.
# ---------------------------------------------------------------------------
target_compile_options(frappe_tests PRIVATE
        -Wno-conversion
        -Wno-sign-conversion
        -Wno-unused-function
)

# ---------------------------------------------------------------------------
# 7. CTest integration — CLion picks up every TEST_CASE as a runnable test
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

catch_discover_tests(frappe_tests
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)