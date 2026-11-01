// Asset validation for CI and the command line (the editor has Assets > Validate assets):
//   asset_check [assets folder] [--models] [--quiet] [--write-registry]
// Lists references to missing files (errors), unused assets and, with --models, glTF
// models whose size does not fit the world scale (1 unit = 1 m). Exit code 1 on errors.
#include <engine/AssetRegistry.h>
#include <engine/Log.h>

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
    std::filesystem::path root = "assets";
    bool models = false, quiet = false, write = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--models") == 0)
            models = true;
        else if (std::strcmp(argv[i], "--write-registry") == 0)
            write = true; // gives new assets their GUID (asset_registry.txt)
        else if (std::strcmp(argv[i], "--quiet") == 0)
            quiet = true;
        else
            root = argv[i];
    }
    engine::AssetRegistry registry;
    std::string error;
    if (!registry.scan(root, &error)) {
        std::fprintf(stderr, "asset_check: %s\n", error.c_str());
        return 2;
    }
    if (write && !registry.save(&error)) {
        std::fprintf(stderr, "asset_check: %s\n", error.c_str());
        return 2;
    }
    const auto issues = registry.validate(models);
    int errors = 0, warnings = 0, infos = 0;
    for (const engine::AssetIssue& issue : issues) {
        const char* level = issue.severity == engine::AssetIssue::Severity::Error     ? "error"
                            : issue.severity == engine::AssetIssue::Severity::Warning ? "warning"
                                                                                      : "info";
        errors += issue.severity == engine::AssetIssue::Severity::Error;
        warnings += issue.severity == engine::AssetIssue::Severity::Warning;
        infos += issue.severity == engine::AssetIssue::Severity::Info;
        if (!quiet || issue.severity != engine::AssetIssue::Severity::Info)
            std::printf("%s: %s: %s\n", level, issue.file.lexically_relative(registry.root()).generic_string().c_str(),
                        issue.message.c_str());
    }
    std::printf("asset_check: %zu assets, %d errors, %d warnings, %d unused\n", registry.records().size(), errors, warnings, infos);
    return errors > 0 ? 1 : 0;
}
