"""Build SoftAP Tailwind/DaisyUI CSS and embed into assets.cpp (upload only)."""
Import("env")

from pathlib import Path
from SCons.Script import COMMAND_LINE_TARGETS
import shutil
import subprocess
import os

UI = Path(env["PROJECT_DIR"]) / "ap_server" / "ui"


def _npm():
    npm = shutil.which("npm")
    if not npm:
        raise SystemExit(
            "build_ap_css: npm not found — install Node.js to build SoftAP CSS"
        )
    return npm


def build_ap_css():
    if not UI.is_dir():
        raise SystemExit(f"build_ap_css: missing {UI}")
    npm = _npm()
    env_vars = os.environ.copy()
    # Ensure local node_modules/.bin is used by npx
    if not (UI / "node_modules").is_dir():
        lock = UI / "package-lock.json"
        cmd = [npm, "ci"] if lock.is_file() else [npm, "install"]
        print(f"build_ap_css: {' '.join(cmd)}")
        subprocess.check_call(cmd, cwd=str(UI), env=env_vars)
    print("build_ap_css: npm run build")
    subprocess.check_call([npm, "run", "build"], cwd=str(UI), env=env_vars)


# Rebuild CSS only when flashing, then compile picks up new assets.cpp.
if "upload" in COMMAND_LINE_TARGETS:
    build_ap_css()
