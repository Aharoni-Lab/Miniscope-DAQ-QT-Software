#!/usr/bin/env python3
"""
Make MiniscopeDAQ.exe self-contained (double-clickable) from a conda build.

conda-forge's windeployqt is broken for the conda layout, so we deploy manually:

  1. Copy the needed Qt plugins directly into <exedir>/<category>/ (platforms,
     imageformats, ...). Qt auto-searches the application directory for these,
     so the platform plugin (loaded before main) is found without any config.
  2. Copy the Qt QML modules into <exedir>/qml/ (main.cpp adds this to the QML
     import path at startup).
  3. Walk the import tables of the exe + every plugin/QML DLL and copy each
     dependency that lives in the conda environment (Qt, OpenCV, FFmpeg,
     Python, image/codec libs, ...) next to the exe. System DLLs are left alone.
  4. Verify completeness.

After this, the exe runs by double-click with no conda env on PATH.

Usage:  python deploy.py <path-to-exe> <conda-prefix>
Requires: pefile (pip install pefile)
"""
import os, sys, shutil, glob
import pefile

EXE = os.path.abspath(sys.argv[1])
ENV = os.path.abspath(sys.argv[2])
exedir = os.path.dirname(EXE)
qt6 = os.path.join(ENV, "Library", "lib", "qt6")

search_dirs = [os.path.join(ENV, "Library", "bin"), ENV, os.path.join(ENV, "DLLs")]
system32 = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "System32")

# Only the plugin categories this GUI app actually needs at runtime. (Avoid
# sqldrivers etc. - they pull deps like libpq that the app never uses and that
# would fail to load if Qt enumerated them.)
PLUGIN_CATS = ["platforms", "imageformats", "iconengines", "styles"]

# DLLs loaded dynamically by NAME (not via import tables), so the dependency
# walker can't discover them. The conda BLAS stack is the case here: OpenCV's
# liblapack/libcblas load libblas, which loads the OpenBLAS backend by name.
# (Requires the env to use the OpenBLAS BLAS variant, not MKL, so nothing pulls
# in mkl_rt and its 565 MB of dynamically-loaded kernels.)
EXTRA_DYNAMIC = ["openblas.dll", "libblas.dll", "liblapack.dll",
                 "libcblas.dll", "liblapacke.dll"]

def is_system(name):
    low = name.lower()
    if low.startswith("api-ms-") or low.startswith("ext-ms-"):
        return True
    return os.path.isfile(os.path.join(system32, name))

def find_in_env(name):
    for d in search_dirs:
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    return None

def imports_of(path):
    try:
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        out = [e.dll.decode(errors="ignore")
               for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []) if e.dll]
        pe.close()
        return out
    except Exception:
        return []

# --- 0. clean any previous deploy layout --------------------------------
for old in ["plugins", "qt.conf"]:
    p = os.path.join(exedir, old)
    if os.path.isdir(p):
        shutil.rmtree(p)
    elif os.path.isfile(p):
        os.remove(p)

# --- 1. plugins -> exedir/<category> ------------------------------------
print("[1/4] copying Qt plugins ...")
for cat in PLUGIN_CATS:
    src = os.path.join(qt6, "plugins", cat)
    if os.path.isdir(src):
        shutil.copytree(src, os.path.join(exedir, cat), dirs_exist_ok=True)
        print("      ", cat)

# --- 2. QML modules -> exedir/qml ---------------------------------------
print("[2/4] copying QML modules ...")
shutil.copytree(os.path.join(qt6, "qml"), os.path.join(exedir, "qml"), dirs_exist_ok=True)

# --- 3. dependency walk -------------------------------------------------
print("[3/4] copying conda dependencies ...")
roots = [EXE]
for cat in PLUGIN_CATS:
    roots += glob.glob(os.path.join(exedir, cat, "**", "*.dll"), recursive=True)
roots += glob.glob(os.path.join(exedir, "qml", "**", "*.dll"), recursive=True)

# Seed the walk with the dynamically-loaded extras (copy them first, overwriting
# any stale copies from a previous deploy / different BLAS variant).
for name in EXTRA_DYNAMIC:
    src = find_in_env(name)
    if src:
        dst = os.path.join(exedir, name)
        shutil.copy2(src, dst)
        roots.append(dst)

copied, processed, queue = set(), set(), list(roots)
while queue:
    cur = queue.pop()
    if cur.lower() in processed:
        continue
    processed.add(cur.lower())
    for dll in imports_of(cur):
        low = dll.lower()
        if is_system(dll) or low in copied:
            continue
        src = find_in_env(dll)
        if not src:
            continue
        # Always overwrite so a re-deploy refreshes DLLs to the current env
        # (e.g. after switching BLAS variants).
        shutil.copy2(src, os.path.join(exedir, dll))
        copied.add(low)
        queue.append(os.path.join(exedir, dll))
print("      copied %d conda DLLs" % len(copied))

# --- 4. verify ----------------------------------------------------------
print("[4/4] verifying ...")
local = {os.path.basename(p).lower()
         for p in glob.glob(os.path.join(exedir, "**", "*.dll"), recursive=True)}
missing = set()
for f in [EXE] + glob.glob(os.path.join(exedir, "**", "*.dll"), recursive=True):
    for dll in imports_of(f):
        if is_system(dll) or dll.lower() in local:
            continue
        if find_in_env(dll):
            missing.add(dll)
if missing:
    print("  WARNING - conda deps not deployed:")
    for m in sorted(missing):
        print("    ", m)
    sys.exit(1)
print("  OK - self-contained. Double-click %s should work." % os.path.basename(EXE))
