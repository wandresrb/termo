# termo developer recipes; `just` alone lists them.

docker := `command -v docker >/dev/null 2>&1 && echo docker || echo podman`
image := "ghcr.io/wandresrb/termo-ci-ubuntu"
tag   := `shasum -a 256 ci/Dockerfile.ubuntu | cut -c1-16`
opts  := "-Db_sanitize=address,undefined -Dbuildtype=debugoptimized -Dwerror=true"

default:
    @just --list

# configure once, then compile
build:
    [ -d build ] || meson setup build {{opts}}
    meson compile -C build

# unit, then lua, then e2e: the same order and flags as CI
test: build
    meson test -C build --print-errorlogs

unit: build
    meson test -C build --suite unit --print-errorlogs

lua: build
    meson test -C build --suite lua --print-errorlogs

# the smoke stage of CI: lua specs plus the e2e cli module
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

# build the CI image locally under the tag CI uses (docker or podman); never push it, the workflow owns the package
image:
    {{docker}} build --pull -f ci/Dockerfile.ubuntu -t {{image}}:{{tag}} ci

# the CI gate inside the image; `docker login ghcr.io` and pull, or `just image` first
ci-local cc="gcc-14":
    {{docker}} run --rm -v "$PWD":/src -w /src -e CC={{cc}} {{image}}:{{tag}} sh -ec 'meson setup build-ci {{opts}} -De2e=enabled && meson compile -C build-ci && meson test -C build-ci --suite unit --print-errorlogs && meson test -C build-ci --suite lua --print-errorlogs && meson test -C build-ci --suite e2e --print-errorlogs'

# clang-tidy in the CI image, zero findings or it fails (the gate runs the same). Mounted at
# /work so the src regex matches; excludes src/compat/ (re-imported OpenBSD code).
tidy:
    {{docker}} run --rm -v "$PWD":/work -w /work -e CC=clang-20 {{image}}:{{tag}} sh -ec 'rm -rf build-tidy; meson setup build-tidy >/dev/null; ninja -C build-tidy cmd-parse.c >/dev/null; run-clang-tidy-20 -p build-tidy -quiet -warnings-as-errors="*" "(src/(?!compat/)|tests/unit/).*\.c$"'
