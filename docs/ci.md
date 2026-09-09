# CI

What runs where, and why. The workflows are `.github/workflows/`; the design is
`docs/sdlc/specs/005-ci-and-e2e.md`.

## The gate: `ci.yml`

Runs on every pull request and on every push to `main`. Five checks are required
by the `main` ruleset: `commit messages`, `clang-tidy`, `gcc-14`, `clang-20`,
`macos`.

```
commit messages (PR only) ─┐
image ─────────────────────┼─► gcc-14 ──┐
                           └─► clang-20 ┴─► macos
```

- `image` computes the tag of each builder image (`sha256sum ci/Dockerfile.<x>`,
  16 hex digits) and builds and pushes to GHCR only when the tag is missing. The
  packages are public: pulling needs no login.
- `clang-tidy` runs the linter over the whole tree (`src/` minus `compat/`, plus
  `tests/unit/`) inside the Ubuntu image, at zero findings: `.clang-tidy` enables
  the checks and turns off, with a reason on each, the ones that flag tmux idioms
  rather than bugs. Enabling a check is one commit that also fixes what it finds.
  `just tidy` runs the same command locally.
- `gcc-14` and `clang-20` run inside `ghcr.io/wandresrb/termo-ci-ubuntu` with
  ASan and UBSan and `-Dwerror`. The stages are steps of one job, in order:
  `configure`, `build`, `unit`, `smoke` (Lua specs and the `cli` e2e module),
  `e2e`. One job per compiler because a job boundary costs about 35 s (image
  pull, checkout, artifact, scheduler) and the whole build takes 40 s; the first
  version had four jobs per compiler and spent 258 s on 90 s of work. ccache lives
  in `actions/cache`, keyed by compiler; a warm cache turns the 66 s compile into
  15 to 20 s.
- `macos` (`macos-26`, Apple clang, Homebrew dependencies, pytest in a venv) runs
  the same five steps after both Linux jobs passed, so it never spends its slower
  minutes on code that fails on Linux. It is the development platform, which is
  why it stays in the gate.

Nothing in the gate installs a toolchain on Linux: `ci/Dockerfile.ubuntu` is the
toolchain, and a change to it is a new image tag. A pull request from a fork runs
with a read-only token, so it can pull an existing image but not push a new one;
the `image` job says so and fails. Dockerfile changes are pushed by a maintainer
from a branch of the repository. `just ci-local [gcc-14|clang-20]` runs the same
steps in the same image with Docker or Podman.

## Nightly: `nightly.yml`

Cron and `workflow_dispatch`: fuzzing (10 min per target), build variants
(`-Dluajit=disabled -Dutf8proc=disabled`, `-Dsixel=true` release), the Alpine musl build, FreeBSD in
`vmactions/freebsd-vm`. The container jobs run on the runner named by the
repository variable `TERMO_LINUX_RUNNER`; unset, that is `ubuntu-24.04`.

## Scanning

- `codeql.yml`: CodeQL `c-cpp`, `security-extended`, built inside the Ubuntu image
  with ccache disabled (a cache hit is a compilation the tracer never sees). On
  push to `main`, on pull requests that touch `src/**` or the Meson files, and
  weekly. Not a required check: with the path filter it does not report on a docs
  PR.
- `scorecard.yml`: OpenSSF Scorecard, weekly and on push to `main`, results
  published and uploaded as SARIF.
- `zizmor.yml`: workflow linting, SARIF to Security → Code scanning, on any change
  under `.github/workflows/`.
- `dependabot.yml`: weekly grouped PRs for GitHub Actions and for the base images
  in `ci/`, commit prefix `ci:`. Dependabot writes commit bodies; the
  `commit messages` check skips its commits and the PR is squashed at merge.
- Repository settings, set once through the API: secret scanning, push
  protection, Dependabot alerts and security updates, private vulnerability
  reporting. The `main` ruleset requires a pull request, resolved review threads
  and the four checks, and forbids force pushes and deletion; the admin role can
  bypass only through a pull request, never with a direct push.

## Self-hosted runners

A self-hosted runner in a public repository executes whatever a pull request
from a fork sends it, and GitHub's own guidance is not to do that. termo's rule:
a self-hosted runner never serves `pull_request`. It serves `push` to `main`,
nightly and `workflow_dispatch`, which is what `TERMO_LINUX_RUNNER` selects; the
gate for pull requests stays on GitHub's hosted runners, which are free for
public repositories.

Recipe, on a Linux host with Podman:

```sh
# the runner talks to a Docker socket for container: jobs
sudo dnf install podman-docker            # or apt install podman-docker
sudo systemctl enable --now podman.socket

# one ccache for every job and branch on this host
sudo mkdir -p /var/cache/termo-ccache && sudo chown runner: /var/cache/termo-ccache

# registration; --ephemeral makes the runner take one job and exit, so a
# systemd unit (or a container) restarts a clean one for the next
./config.sh --url https://github.com/wandresrb/termo --token <token> \
  --labels termo-linux --ephemeral --unattended
```

Then set the repository variable: `gh variable set TERMO_LINUX_RUNNER
--body termo-linux`. For the ccache to be shared, export `CCACHE_DIR` and
`CCACHE_BASEDIR=$GITHUB_WORKSPACE` in the runner's `.env` file; `base_dir` makes
different checkout paths produce the same cache keys. With a persistent
workspace (`actions/checkout` only rewrites files that changed) ninja rebuilds
only what changed, which is the incremental build hosted runners cannot have: a
hosted runner is a fresh VM whose checkout carries today's mtimes, so a restored
`build/` would be rebuilt from scratch anyway. That is why the hosted gate caches
the compiler (ccache, content-addressed) and not the build tree.
