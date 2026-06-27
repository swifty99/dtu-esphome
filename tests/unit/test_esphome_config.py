import shutil
import subprocess
from pathlib import Path


def test_hm4_readonly_config_validates():
    esphome = shutil.which("esphome")
    assert esphome is not None, "esphome executable not found"
    repo = Path(__file__).parents[2]
    config = repo / "tests" / "components" / "hoymiles_dtu" / "test.hm4-readonly.yaml"
    result = subprocess.run(
        [esphome, "config", str(config)],
        cwd=repo,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    assert result.returncode == 0, result.stdout
