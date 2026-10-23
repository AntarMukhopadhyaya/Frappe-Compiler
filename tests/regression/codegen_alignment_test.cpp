//
// Created by antar on 22/10/26.
//
// Regression guard: prior to the TargetMachine-based datalayout fix,
// emit_llvm_ir produced "store i64 ..., align 4" while allocas said
// "align 8". That's harmless on x86_64 but undefined behavior on
// strict-alignment targets (aarch64). If anyone ever reverts
// mod->setDataLayout(tm->createDataLayout()) or reinstates a
// hardcoded/empty layout, this test fires immediately.
//

#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"
#include "../helpers/codegen_test.hpp"

TEST_CASE("codegen: i64 loads and stores use align 8, not align 4",
          "[codegen][regression][alignment]") {
    auto r = frappe_test::compile(R"(
fn main() -> Int {
    let x: Int = 42;
    return x;
}
)");
    REQUIRE_FALSE(r->has_errors());

    auto ir = frappe_test::build_ir(*r);
    if (!ir.has_value()) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    // Sanity: the program must actually emit i64 load/store pairs.
    REQUIRE(frappe_test::ir_contains(*ir, "store i64"));
    REQUIRE(frappe_test::ir_contains(*ir, "load i64"));

    // The actual guard.
    REQUIRE(frappe_test::has_consistent_alignments(*ir));
}