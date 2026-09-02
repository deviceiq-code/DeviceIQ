"""PlatformIO post-build hook: packages the combined OTA file automatically.

Registered via platformio.ini's extra_scripts. Fires after either
firmware.bin (pio run) or littlefs.bin (pio run --target buildfs) is
rebuilt; runs scripts/package_ota.py only once both files are present, so
it's a no-op after whichever of the two builds finishes first.
"""

import subprocess
import sys
from pathlib import Path

Import("env")  # noqa: F821 - injected by PlatformIO


def maybe_package_ota(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    firmware = build_dir / "firmware.bin"
    filesystem = build_dir / "littlefs.bin"

    if not firmware.is_file() or not filesystem.is_file():
        missing = "littlefs.bin" if firmware.is_file() else "firmware.bin"
        print(f"package_ota: waiting on {missing} - skipped for now")
        return

    project_dir = Path(env.subst("$PROJECT_DIR"))
    script = project_dir / "scripts" / "package_ota.py"

    result = subprocess.run(
        [sys.executable, str(script), "--firmware", str(firmware), "--filesystem", str(filesystem)],
        cwd=str(project_dir),
    )
    if result.returncode != 0:
        print("package_ota: packaging failed, see output above")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", maybe_package_ota)
env.AddPostAction("$BUILD_DIR/littlefs.bin", maybe_package_ota)
