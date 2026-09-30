from __future__ import annotations

import itertools
import os
import subprocess
from pathlib import Path

import pytest

from termo import ROOT, Server

counter = itertools.count(1)


def pytest_addoption(parser):
    parser.addoption("--termo", default=None, help="termo binary under test")
    parser.addoption("--keep", action="store_true",
                     help="leave the server of a failed test running")


def pytest_configure(config):
    path = config.getoption("--termo") or os.environ.get("TERMO_BIN") or ROOT / "build/termo"
    config.termo_bin = Path(path).resolve()
    if not os.access(config.termo_bin, os.X_OK):
        raise pytest.UsageError(f"not executable: {config.termo_bin}")
    config.termo_keep = config.getoption("--keep") or bool(os.environ.get("TERMO_E2E_KEEP"))


@pytest.fixture(scope="session")
def termo_bin(request) -> Path:
    return request.config.termo_bin


@pytest.fixture(scope="session")
def project_version() -> str:
    text = (ROOT / "meson.build").read_text()
    for line in text.splitlines():
        if line.strip().startswith("version:"):
            return line.split("'")[1]
    raise RuntimeError("version not found in meson.build")


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    if rep.when == "call":
        item.failed = rep.failed
        for server in getattr(item, "servers", []):
            for report in server.reports:
                rep.sections.append((f"sanitizer report {report.name}", report.read_text()))


@pytest.fixture
def make_server(termo_bin, tmp_path, request):
    servers: list[Server] = []
    request.node.servers = servers

    def factory(name: str | None = None) -> Server:
        label = name or f"e2e-{os.getpid()}-{next(counter)}"
        s = Server(termo_bin, label, tmpdir=tmp_path, keep=request.config.termo_keep)
        servers.append(s)
        return s

    yield factory
    failed = getattr(request.node, "failed", False)
    for s in servers:
        s.kill(failed=failed)


@pytest.fixture
def server(make_server) -> Server:
    return make_server()


def pytest_sessionfinish(session, exitstatus):
    if os.environ.get("TERMO_E2E_UNDER_MESON") and exitstatus == pytest.ExitCode.NO_TESTS_COLLECTED:
        session.exitstatus = 0
