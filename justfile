# termo developer recipes; `just` alone lists them.

opts  := "-Db_sanitize=address,undefined -Dbuildtype=debugoptimized -Dwerror=true"

default:
    @just --list

# configure once, then compile
build:
    [ -d build ] || meson setup build {{opts}}
    meson compile -C build

# unit, then lua, then e2e
test: build
    meson test -C build --print-errorlogs

unit: build
    meson test -C build --suite unit --print-errorlogs

lua: build
    meson test -C build --suite lua --print-errorlogs

# smoke: lua specs plus the e2e cli module
smoke: build
    meson test -C build --suite lua --suite smoke --print-errorlogs

e2e: build
    meson test -C build --suite e2e --print-errorlogs

# upstream tmux's regress/ against build/termo, before a release; script names narrow the run
upstream-regress *args: build
    git fetch -q upstream master
    rm -rf build/upstream-regress && mkdir -p build/upstream-regress
    git archive upstream/master regress | tar -x -C build/upstream-regress
    python3 tools/regress-runner.py build/termo {{args}}

# clang-tidy, zero findings or it fails; excludes src/compat/ (re-imported OpenBSD code)
tidy:
    rm -rf build-tidy; CC=clang meson setup build-tidy >/dev/null; ninja -C build-tidy cmd-parse.c >/dev/null; run-clang-tidy -p build-tidy -quiet -warnings-as-errors="*" "$PWD/(src/(?!compat/)|tests/unit/).*\.c$"

# rustfmt and clippy on src/rs through Meson
rs-lint: build
    rustfmt --check --edition 2024 --config-path src/rs/rustfmt.toml src/rs/lib.rs src/rs/build.rs
    ninja -C build clippy

# local install, XDG-friendly default prefix; override with `PREFIX=/opt/termo just install`
install prefix=env_var_or_default("PREFIX", env_var("HOME") / ".local"):
    meson setup build-release --buildtype=release --prefix="{{prefix}}" -Db_sanitize=none
    ninja -C build-release
    meson install -C build-release


# VT throughput benchmark on a release build without sanitizers; results in build/bench/<sha>.json
bench *args:
    [ -d build-bench ] || meson setup build-bench -Dbuildtype=release -Db_sanitize=none
    meson compile -C build-bench
    python3 tools/bench/bench.py --termo build-bench/termo {{args}}

# the 10% gate: compare build/bench/<sha>.json against the current HEAD's file
bench-compare sha:
    python3 tools/bench/bench.py --compare {{sha}}
