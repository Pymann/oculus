// oculus: placeholder so CI has something to build.  The architecture (frontend
// integration, IR, module layout) is not decided yet; see CLAUDE.md.
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* kVersion = "0.0.1";

int usage(std::FILE* out) {
    std::fprintf(out,
                 "usage: oculus [--version] <file.c>...\n"
                 "Diagnostics: file:line:col: RULE-ID: message\n");
    return out == stderr ? 2 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage(stderr);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("oculus %s\n", kVersion);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) return usage(stdout);
    }
    // No rule is implemented yet, so no file has a finding.
    return 0;
}
