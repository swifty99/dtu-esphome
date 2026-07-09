import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).parents[2]
CONFIG_DIR = REPO / "tests" / "components" / "hoymiles_dtu"

CONFIGS = [
    "test.minimal.yaml",
    "test.hm4-readonly.yaml",
    "test.multi-model.yaml",
    "test.scan-detect.yaml",
    "test.scan.yaml",
]


@pytest.mark.parametrize("config_name", CONFIGS)
def test_config_validates(config_name):
    esphome = shutil.which("esphome")
    if esphome is None:
        pytest.skip("esphome executable not found")
    config = CONFIG_DIR / config_name
    result = subprocess.run(
        [esphome, "config", str(config)],
        cwd=REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    assert result.returncode == 0, result.stdout
