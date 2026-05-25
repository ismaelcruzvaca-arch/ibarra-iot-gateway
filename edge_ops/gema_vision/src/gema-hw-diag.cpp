/**
 * @file gema-hw-diag.cpp
 * @brief Hardware diagnostics micro-binary for RV1106 FAT.
 *
 * Tests two hardware blocks that cannot be accessed from Bash:
 *   1. NPU — rknn_init() symbol presence via dlopen
 *   2. RGA — device node /dev/rga accessibility
 *
 * Exit codes:
 *   0 — all requested tests passed
 *   1 — any requested test failed
 *
 * Usage:
 *   gema-hw-diag              → run both NPU + RGA tests
 *   gema-hw-diag --npu        → run only NPU test
 *   gema-hw-diag --rga        → run only RGA test
 *
 * On x86_64, all tests SKIP (return 0) — the binary compiles but
 * hardware is only present on RV1106.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __arm__
#include <dlfcn.h>
#endif

// ===========================================================================
// NPU test
// ===========================================================================

#ifdef __arm__
static int test_npu()
{
    // 1. Check NPU device node.
    FILE* fp = std::fopen("/dev/rknpu0", "r");
    if (!fp) {
        fp = std::fopen("/dev/dri/card0", "r");
    }
    if (!fp) {
        std::fprintf(stderr, "FAIL: NPU device node not found "
                             "(/dev/rknpu0 or /dev/dri/card0)\n");
        return 1;
    }
    std::fclose(fp);

    // 2. Try to load librknn_api.so and resolve rknn_init.
    void* handle = dlopen("librknn_api.so", RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        std::fprintf(stderr, "FAIL: librknn_api.so not loadable\n");
        return 1;
    }

    void* sym = dlsym(handle, "rknn_init");
    if (!sym) {
        std::fprintf(stderr, "FAIL: rknn_init symbol not found "
                             "in librknn_api.so\n");
        dlclose(handle);
        return 1;
    }

    dlclose(handle);
    std::fprintf(stdout, "PASS: NPU device node found, "
                         "librknn_api.so loadable\n");
    return 0;
}
#else
static int test_npu()
{
    std::fprintf(stdout, "SKIP: NPU test not available on x86_64\n");
    return 0;
}
#endif

// ===========================================================================
// RGA test
// ===========================================================================

#ifdef __arm__
static int test_rga()
{
    FILE* fp = std::fopen("/dev/rga", "r");
    if (!fp) {
        std::fprintf(stderr, "FAIL: /dev/rga not found\n");
        return 1;
    }
    std::fclose(fp);

    std::fprintf(stdout, "PASS: /dev/rga accessible\n");
    return 0;
}
#else
static int test_rga()
{
    std::fprintf(stdout, "SKIP: RGA test not available on x86_64\n");
    return 0;
}
#endif

// ===========================================================================
// Entry point
// ===========================================================================

static void print_usage(const char* argv0)
{
    std::fprintf(stdout,
        "GEMA Hardware Diagnostics v0.1\n"
        "Usage: %s [--npu] [--rga] [--help]\n"
        "\n"
        "  (no args)   Run all available hardware tests\n"
        "  --npu       Run only NPU test\n"
        "  --rga       Run only RGA test\n"
        "  --help      Show this message\n",
        argv0);
}

int main(int argc, char** argv)
{
    bool run_npu = true;
    bool run_rga = true;

    if (argc > 1) {
        run_npu = false;
        run_rga = false;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--npu") == 0)       run_npu = true;
            else if (std::strcmp(argv[i], "--rga") == 0)  run_rga = true;
            else if (std::strcmp(argv[i], "--help") == 0) {
                print_usage(argv[0]);
                return 0;
            } else {
                std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        }
    }

    int exit_code = 0;

    std::fprintf(stdout, "=== GEMA Hardware Diagnostics ===\n\n");

    if (run_npu) {
        std::fprintf(stdout, "[1/2] NPU ................... ");
        std::fflush(stdout);
        int rc = test_npu();
        if (rc != 0) exit_code = 1;
    }

    if (run_rga) {
        std::fprintf(stdout, "[2/2] RGA ................... ");
        std::fflush(stdout);
        int rc = test_rga();
        if (rc != 0) exit_code = 1;
    }

    std::fprintf(stdout, "\n");
    if (exit_code == 0) {
        std::fprintf(stdout, "=== ALL HARDWARE DIAGNOSTICS PASSED ===\n");
    } else {
        std::fprintf(stdout, "=== SOME TESTS FAILED ===\n");
    }

    return exit_code;
}
