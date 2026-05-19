# AGenUI C++ test suite

Standalone gtest-based test suite for the platform-agnostic C++ core in
`core/`. Licensed under the same terms as the main project
([Apache 2.0](../../LICENSE)). Tests cover:

- **integration** — end-to-end through the public engine + SurfaceManager API
- **unit** — white-box tests for individual modules (parsers, dispatchers, …)
- **concurrency** — thread-safety + lifecycle race tests
- **stress** — short in-process pressure tests (~minutes)
- **sanitizer** — tests purpose-built to surface ASan / UBSan findings

The suite is **standalone**: it pulls gtest 1.14 and yoga 2.0 via
`FetchContent` and compiles `core/src/**` as a static library. It does
not modify or rebuild `platforms/`, `playground/`, or any existing test
tree.

## Quick start (host)

By default the convenience script runs a **plain build without sanitizers**.
Sanitizer builds can be enabled explicitly via flags:

```bash
./tests/cpp/ci/run_tests.sh                       # plain build (default)
./tests/cpp/ci/run_tests.sh --asan-only           # ASan + UBSan
./tests/cpp/ci/run_tests.sh --no-san              # plain build (explicit)
```

Or invoke CMake directly for finer control:

```bash
cmake -S tests/cpp -B tests/cpp/build/host        # default: no sanitizer
cmake --build tests/cpp/build/host -j 4
ctest --test-dir tests/cpp/build/host --output-on-failure
```

## Build options

| CMake option | Default | Notes |
|---|---|---|
| `AGENUI_TESTS_ENABLE_ASAN` | `OFF` | ASan + UBSan |
| `AGENUI_TESTS_ENABLE_COVERAGE` | `OFF` | gcov instrumentation |
| `AGENUI_TESTS_USE_LOCAL_YOGA` | `OFF` | Skip GitHub fetch; supply `AGENUI_TESTS_LOCAL_YOGA_DIR` |

See [`BUILD.md`](./BUILD.md) for full build details, sanitizer matrix,
and coverage report generation.

## What's covered

| Suite | Files | Tests |
|---|---|---|
| `integration/` | 8 | 58 |
| `unit/` | 17 | 157 |
| `concurrency/` | 6 | 26 |
| `stress/` | 1 | 2 |
| `sanitizer/memory_safety_test.cpp` | 1 | 3 |

Total: **~246 tests**, all green under the default host (plain) build.

The executables split as follows:

| Executable | Sources | Tests |
|---|---|---|
| `agenui_unit_tests` | `integration/` + `unit/` + `stress/` + `sanitizer/memory_safety_test.cpp` | 220 |
| `agenui_stream_tests` | `unit/stream/**/*.cpp` | 114 |
| `agenui_concurrency_tests` | `concurrency/` + `sanitizer/thread_sanitizer_test.cpp` | 28 |

> `sanitizer/thread_sanitizer_test.cpp` is compiled into `agenui_concurrency_tests`
> and passes under the plain build. ThreadSanitizer (`-DAGENUI_TESTS_ENABLE_TSAN=ON`)
> is not enabled by default and is not supported in CI.

## IDE debugging

| IDE | Doc |
|---|---|
| Xcode (macOS host) | [`ide/xcode/README.md`](./ide/xcode/README.md) |

Quick recipe:

```bash
# Xcode (Debug = ASan, Release = plain):
./tests/cpp/ide/xcode/generate.sh
open tests/cpp/build/xcode/agenui_cpp_tests.xcodeproj
# pick scheme `agenui_unit_tests`, ⌘R to run, set breakpoints in core/src/**.
```

## Adding a new test

1. Create a new `.cpp` file in the right subdirectory:
   - Black-box, public API only → `integration/`
   - White-box, internal class → `unit/<area>/`
   - Multi-thread → `concurrency/`
2. Use the test fixtures in `support/`:
   - `ScopedSurfaceManager` for per-test SurfaceManager isolation
   - `MockMessageListener` to capture callbacks
   - `WaitForWorkerIdle()` to synchronize with the engine's worker thread
3. Re-run CMake configure (file globs are resolved once):
   ```bash
   cmake -S tests/cpp -B tests/cpp/build
   ```
4. Build & run; if you added a new fixture file, update `fixtures/`.

## Layout

```
tests/cpp/
├── CMakeLists.txt          # build
├── main.cpp                # gtest entry + global engine env
├── support/                # mocks, helpers, fixture loader
├── fixtures/               # protocol/json fixtures
├── integration/            # 8 files, 58 tests
├── unit/
│   ├── module/             # message thread, dispatcher, thread manager
│   ├── stream/             # parsers, plugins, extractors
│   └── function_call/      # builtin function registry
├── concurrency/            # 6 files, 26 tests
├── stress/                 # in-process stress
├── sanitizer/              # ASan + UBSan dedicated
├── ide/                    # Xcode
├── ci/                     # run_tests.sh + GitHub Actions template
└── BUILD.md                # build / sanitizer / coverage details
```
