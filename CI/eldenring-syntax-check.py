"""Syntax-check named C++ files using the an already-installed libclang shared library.

Usage: python3 CI/eldenring-syntax-check.py file.cpp [other.cpp] -- [compiler flags]
Caller compiler flags follow the default C++20/header flags, so -std=c++17 can override.
"""
import ctypes
import ctypes.util
import os
from pathlib import Path
import sys


class String(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("flags", ctypes.c_uint)]


root = Path(__file__).resolve().parents[1]
tools = root / "build-tools-local"
header_root = tools / "engine-headers"
lib_path = os.environ.get("OPENMW_LIBCLANG")
if not lib_path:
    candidates = list((Path(sys.prefix) / "lib").glob("libclang.so*"))
    lib_path = str(candidates[0]) if candidates else ctypes.util.find_library("clang")
if not lib_path:
    raise SystemExit("libclang is unavailable. Set OPENMW_LIBCLANG to an installed shared library.")
lib = ctypes.CDLL(lib_path)
lib.clang_createIndex.argtypes = [ctypes.c_int, ctypes.c_int]
lib.clang_createIndex.restype = ctypes.c_void_p
lib.clang_parseTranslationUnit.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_char_p), ctypes.c_int, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint]
lib.clang_parseTranslationUnit.restype = ctypes.c_void_p
lib.clang_getNumDiagnostics.argtypes = [ctypes.c_void_p]
lib.clang_getNumDiagnostics.restype = ctypes.c_uint
lib.clang_getDiagnostic.argtypes = [ctypes.c_void_p, ctypes.c_uint]
lib.clang_getDiagnostic.restype = ctypes.c_void_p
lib.clang_getDiagnosticSeverity.argtypes = [ctypes.c_void_p]
lib.clang_getDiagnosticSeverity.restype = ctypes.c_uint
lib.clang_formatDiagnostic.argtypes = [ctypes.c_void_p, ctypes.c_uint]
lib.clang_formatDiagnostic.restype = String
lib.clang_getCString.argtypes = [String]
lib.clang_getCString.restype = ctypes.c_char_p
lib.clang_disposeString.argtypes = [String]
lib.clang_disposeDiagnostic.argtypes = [ctypes.c_void_p]
lib.clang_disposeTranslationUnit.argtypes = [ctypes.c_void_p]
lib.clang_disposeIndex.argtypes = [ctypes.c_void_p]

arguments = sys.argv[1:]
boundary = arguments.index("--") if "--" in arguments else len(arguments)
filenames = arguments[:boundary]
extra = arguments[boundary + 1:]
if not filenames:
    raise SystemExit("Supply C++ source files, optionally followed by -- and additional compiler flags.")
args = ["-x", "c++", "-std=c++20", "-nostdinc++"]
for include in (tools / "clang/include/c++/v1", tools / "clang/include/x86_64-unknown-linux-gnu/c++/v1",
                tools / "clang/lib/clang/18/include", tools / "gcc/sysroot/usr/include",
                tools / "gcc/sysroot/usr/include/x86_64-linux-gnu"):
    args += ["-isystem", str(include)]
for include in (root, root / "extern/oics/include", root / "extern/oics/tinyxml",
                root / "extern/sol_config", root / "extern/sol3"):
    args += ["-I", str(include)]
for dependency in ("osg", "bullet", "sdl", "mygui", "Recast", "Detour", "DetourCrowd", "DetourTileCache", "lua"):
    args += ["-isystem", str(header_root / dependency)]
args += extra

flags = (ctypes.c_char_p * len(args))(*[arg.encode() for arg in args])
index = lib.clang_createIndex(0, 0)
failed = False
for filename in filenames:
    unit = lib.clang_parseTranslationUnit(index, str(Path(filename).resolve()).encode(), flags, len(args), None, 0, 0)
    if not unit:
        print(filename + ": parsing failed")
        failed = True
        continue
    count = lib.clang_getNumDiagnostics(unit)
    errors = 0
    for n in range(count):
        diagnostic = lib.clang_getDiagnostic(unit, n)
        output = lib.clang_formatDiagnostic(diagnostic, 3)
        print(lib.clang_getCString(output).decode())
        errors += lib.clang_getDiagnosticSeverity(diagnostic) >= 3
        lib.clang_disposeString(output)
        lib.clang_disposeDiagnostic(diagnostic)
    print(filename + ": " + str(count) + " diagnostics, " + str(errors) + " errors", flush=True)
    failed |= errors > 0
    lib.clang_disposeTranslationUnit(unit)
lib.clang_disposeIndex(index)
raise SystemExit(int(failed))
