import pathlib
import sys

import pytest

THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from harness import build_all_once, missing_tools


@pytest.fixture(scope="session", autouse=True)
def built_artifacts() -> None:
    missing = missing_tools()
    if missing:
        pytest.skip(f"Missing required toolchain for e2e tests: {', '.join(missing)}")

    build_all_once()
