import pathlib
import signal
import sys

import pytest

THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from harness import build_all_once, missing_tools

DEFAULT_TEST_TIMEOUT_S = 45


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line(
        "markers",
        "e2e_timeout(seconds): override the default e2e per-test timeout",
    )


@pytest.fixture(scope="session", autouse=True)
def built_artifacts() -> None:
    missing = missing_tools()
    if missing:
        pytest.skip(f"Missing required toolchain for e2e tests: {', '.join(missing)}")

    build_all_once()


@pytest.fixture(autouse=True)
def e2e_test_timeout(request: pytest.FixtureRequest):
    if not hasattr(signal, "SIGALRM"):
        yield
        return

    marker = request.node.get_closest_marker("e2e_timeout")
    timeout_s = DEFAULT_TEST_TIMEOUT_S
    if marker and marker.args:
        timeout_s = int(marker.args[0])

    def fail_on_timeout(signum, frame):  # noqa: ARG001
        raise TimeoutError(f"e2e test exceeded {timeout_s}s timeout")

    previous = signal.signal(signal.SIGALRM, fail_on_timeout)
    signal.alarm(timeout_s)
    try:
        yield
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, previous)
