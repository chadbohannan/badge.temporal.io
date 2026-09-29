"""Adds SDL2 to the host harness build when pkg-config can find it.

Without SDL2 the harness still builds and scripted (headless) runs work; only the
live window is missing.
"""

import subprocess

Import("env")  # type: ignore[name-defined]  # noqa: F821

# GCC 15 defaults C to C23, where qrcode.h's own `typedef unsigned char bool` is
# an error. Pin the language levels per language.
env.Append(CXXFLAGS=["-std=gnu++20"], CFLAGS=["-std=gnu11"])  # type: ignore[name-defined]  # noqa: F821

# The MicroPython C sources rely on alloca() and free() coming in through the
# ESP32 toolchain's headers.
env.Append(CFLAGS=["-include", "stdlib.h", "-include", "alloca.h"])  # type: ignore[name-defined]  # noqa: F821


# MicroPython's garbage collector scans the C stack conservatively, reading words
# AddressSanitizer has poisoned as redzones. Leave the collector itself
# uninstrumented; everything else keeps the sanitizers.
def _no_asan(env, node):
    return env.Object(node, CCFLAGS=env["CCFLAGS"] + ["-fno-sanitize=address"])


env.AddBuildMiddleware(_no_asan, "*/py/gc.c")  # type: ignore[name-defined]  # noqa: F821
env.AddBuildMiddleware(_no_asan, "*/py/gchelper_generic.c")  # type: ignore[name-defined]  # noqa: F821
env.AddBuildMiddleware(_no_asan, "*/py/gchelper_native.c")  # type: ignore[name-defined]  # noqa: F821

try:
    cflags = subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True).split()
    libs = subprocess.check_output(["pkg-config", "--libs", "sdl2"], text=True).split()
    env.Append(CCFLAGS=cflags + ["-DHOST_HAVE_SDL"], LINKFLAGS=libs)  # type: ignore[name-defined]  # noqa: F821
except (OSError, subprocess.CalledProcessError):
    print("[host_flags] SDL2 not found; building without the live window")
