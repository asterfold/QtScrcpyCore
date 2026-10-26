"""Run wheel mapping regression tests against the configured local CMake build."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
flags = (root / "build/QtScrcpy/QtScrcpyCore/CMakeFiles/QtScrcpyCore.dir/flags.make").read_text()
includes = shlex.split(next(line.split(" = ", 1)[1] for line in flags.splitlines() if line.startswith("CXX_INCLUDES = ")))
link = shlex.split((root / "build/QtScrcpy/CMakeFiles/QtScrcpy.dir/link.txt").read_text())
libraries = link[next(i for i, value in enumerate(link) if value.startswith("-F")):]
cache = (root / "build/CMakeCache.txt").read_text().splitlines()
sysroot = next(line.split("=", 1)[1] for line in cache if line.startswith("CMAKE_OSX_SYSROOT:"))
env = {**os.environ, "QT_QPA_PLATFORM": "offscreen",
       "DYLD_LIBRARY_PATH": str(root / "QtScrcpy/QtScrcpyCore/src/third_party/ffmpeg/lib/arm64")}
with tempfile.TemporaryDirectory(prefix="qtscrcpy-wheel-") as directory:
    for name in ("wheel_mapping_tests",):
        binary = str(Path(directory) / name)
        subprocess.run(["/usr/bin/c++", "-std=c++11", "-fPIC", "-isysroot", sysroot, *includes,
                        str(Path(__file__).parent / (name + ".cpp")), "-o", binary, *libraries], check=True)
        subprocess.run([binary, str(root / "keymap/三角洲脚本new.json")], env=env, check=True)
