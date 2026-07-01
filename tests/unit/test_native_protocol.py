"""Compile and run the native C++ protocol test against the real component sources.

This keeps the on-device decoder (components/hoymiles_dtu/protocol.cpp) under test
without a parallel Python re-implementation. Skipped when no C++ compiler exists.
"""

import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).parents[2]
COMPONENT = REPO / "components" / "hoymiles_dtu"
NATIVE = REPO / "tests" / "native"


def _find_compiler() -> str | None:
    for candidate in ("c++", "g++", "clang++"):
        path = shutil.which(candidate)
        if path is not None:
            return path
    return None


def test_native_protocol(tmp_path):
    compiler = _find_compiler()
    if compiler is None:
        pytest.skip("no C++ compiler available")

    binary = tmp_path / "test_protocol"
    compile_cmd = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{COMPONENT}",
        f"-I{NATIVE / 'stubs'}",
        str(NATIVE / "test_protocol.cpp"),
        str(COMPONENT / "protocol.cpp"),
        "-o",
        str(binary),
    ]
    compile_result = subprocess.run(
        compile_cmd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    assert compile_result.returncode == 0, compile_result.stdout

    run_result = subprocess.run(
        [str(binary)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    assert run_result.returncode == 0, run_result.stdout
