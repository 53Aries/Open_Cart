# Open_Cart: build and run the host-side unit tests (test/*.cpp) with a desktop C++ compiler.
# Usage: python scripts/run_tests.py     (exit code 0 = all passed)
# Needs g++ or clang++ on PATH (on Windows: MSYS2/MinGW "pacman -S mingw-w64-x86_64-gcc", or LLVM).
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Each test and the pure-logic sources it needs (no ESP-IDF headers).
TESTS = {
    "test_scale": ["test/test_scale.cpp", "src/scale.cpp"],
    "test_unload": ["test/test_unload.cpp", "src/unload.cpp"],
}


def find_compiler():
    for name in ("g++", "clang++", "c++"):
        path = shutil.which(name)
        if path:
            return path
    sys.exit("No host C++ compiler found (g++ or clang++). Install one to run the tests.")


def main():
    cxx = find_compiler()
    failed = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, sources in TESTS.items():
            exe = os.path.join(tmp, name + (".exe" if os.name == "nt" else ""))
            cmd = [cxx, "-std=c++17", "-Wall", "-Wextra", "-I", os.path.join(ROOT, "src"), "-o", exe]
            cmd += [os.path.join(ROOT, s) for s in sources]
            print("== %s" % name)
            if subprocess.run(cmd).returncode != 0:
                failed.append(name + " (build)")
                continue
            if subprocess.run([exe]).returncode != 0:
                failed.append(name)
    if failed:
        sys.exit("FAILED: " + ", ".join(failed))
    print("All host tests passed.")


if __name__ == "__main__":
    main()
