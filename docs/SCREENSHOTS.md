# Screenshot capture and verification

GTLibCpp is a library, not a GUI application. Do not depict a nonexistent dashboard.

1. Run `cmake -S . -B build -DGTLIBCPP_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.
2. Run `cmake --build build --config Release` and `ctest --test-dir build --output-on-failure`.
3. Capture `gtctl --version` and `gtctl --health`, clearly labeling health as a CLI self-test rather than a proof of target attachment.
4. On Windows, optionally capture an authorized read-only sample against an offline test process.
5. Save genuine redacted captures under `docs/images/` and link them only after the builds or tests have genuinely succeeded.

Include OS, compiler, build mode and the relevant commit hash in each screenshot caption. Do not present CI badges as substitutes for actual run output.
