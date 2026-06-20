#!/usr/bin/env python3
"""
Assemble a clean, standalone, double-clickable distribution from a conda build.

Output layout (flat) under <build>/Release/dist/:

    dist/
      MiniscopeDAQ.exe    <- the application (double-click this)
      *.dll               <- Qt, OpenCV, OpenBLAS, Python, ... (bundled)
      platforms/ imageformats/ iconengines/ styles/   <- Qt plugins
      qml/                <- Qt QML modules
      deviceConfigs/ userConfigs/ Scripts/            <- runtime data

Everything lives in one folder: the exe finds its DLLs (same dir), its Qt
plugins (<appdir>/<category>), its QML modules (<appdir>/qml, see main.cpp),
and its configs (the working dir is the exe's folder on double-click). The
whole dist/ folder is portable - copy it to any Windows PC and double-click
MiniscopeDAQ.exe; no conda env required.

conda-forge's windeployqt is broken for the conda layout, so we deploy manually:
copy the needed plugins + QML modules, then walk the import graph and copy every
conda DLL dependency. The dynamically-loaded OpenBLAS BLAS chain (which import
scanning can't see) is copied explicitly. The env must use OpenBLAS, not MKL.

Usage:  python deploy.py <real-exe> <conda-prefix>
Requires: pefile (pip install pefile)
"""
import os, sys, shutil, glob
import pefile

EXE = os.path.abspath(sys.argv[1])
ENV = os.path.abspath(sys.argv[2])

srcdir = os.path.dirname(EXE)
dist = os.path.join(srcdir, "dist")
qt6 = os.path.join(ENV, "Library", "lib", "qt6")

search_dirs = [os.path.join(ENV, "Library", "bin"), ENV, os.path.join(ENV, "DLLs")]
system32 = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "System32")

PLUGIN_CATS = ["platforms", "imageformats", "iconengines", "styles"]
DATA_DIRS = ["deviceConfigs", "userConfigs", "Scripts"]
# Dynamically-loaded by name (invisible to import-table scanning): the conda
# OpenBLAS BLAS chain that OpenCV uses. Requires the OpenBLAS BLAS variant (not
# MKL) - see CMakeLists.txt / environment.yml.
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

# --- fresh dist (flat: exe + everything in one folder) -------------------
print("[1/5] preparing dist ...")
if os.path.isdir(dist):
    shutil.rmtree(dist)
os.makedirs(dist)
shutil.copy2(EXE, os.path.join(dist, os.path.basename(EXE)))
for d in DATA_DIRS:
    s = os.path.join(srcdir, d)
    if os.path.isdir(s):
        shutil.copytree(s, os.path.join(dist, d))

# --- plugins + QML -------------------------------------------------------
print("[2/5] copying Qt plugins ...")
for cat in PLUGIN_CATS:
    s = os.path.join(qt6, "plugins", cat)
    if os.path.isdir(s):
        shutil.copytree(s, os.path.join(dist, cat))
print("[3/5] copying QML modules ...")
shutil.copytree(os.path.join(qt6, "qml"), os.path.join(dist, "qml"))

# --- dependency walk -----------------------------------------------------
print("[4/5] copying conda dependencies ...")
roots = [os.path.join(dist, os.path.basename(EXE))]
for cat in PLUGIN_CATS:
    roots += glob.glob(os.path.join(dist, cat, "**", "*.dll"), recursive=True)
roots += glob.glob(os.path.join(dist, "qml", "**", "*.dll"), recursive=True)
for name in EXTRA_DYNAMIC:
    s = find_in_env(name)
    if s:
        d = os.path.join(dist, name)
        shutil.copy2(s, d)
        roots.append(d)
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
        s = find_in_env(dll)
        if not s:
            continue
        shutil.copy2(s, os.path.join(dist, dll))
        copied.add(low)
        queue.append(os.path.join(dist, dll))
print("      copied %d conda DLLs" % len(copied))

# --- verify --------------------------------------------------------------
print("[5/5] verifying ...")
local = {os.path.basename(p).lower()
         for p in glob.glob(os.path.join(dist, "**", "*.dll"), recursive=True)}
missing = set()
for f in [os.path.join(dist, os.path.basename(EXE))] + \
         glob.glob(os.path.join(dist, "**", "*.dll"), recursive=True):
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
print("  OK -> %s" % dist)
print("  Double-click dist/MiniscopeDAQ.exe (or copy the whole dist/ folder anywhere).")
