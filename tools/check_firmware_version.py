#!/usr/bin/env python3
"""Keep the runtime version and ESP-IDF image descriptor in sync."""

from pathlib import Path
import re
import sys


def check_versions(root):
    cmake = (root / "CMakeLists.txt").read_text()
    flags = (root / "platformio.ini").read_text().replace('\\"', '"')
    descriptor = re.findall(r'set\s*\(\s*PROJECT_VER\s+"([^"]+)"\s*\)', cmake)
    runtime = re.findall(r'-D(STICKY_(?:FIRMWARE|SALAH)_VERSION)="([^"]+)"', flags)
    if len(descriptor) != 1 or len(runtime) != 1:
        raise ValueError("Expected one PROJECT_VER and one runtime version flag")
    macro, version = runtime[0]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("Release version must use MAJOR.MINOR.PATCH")
    if descriptor[0] != version:
        raise ValueError(f"PROJECT_VER={descriptor[0]} differs from {macro}={version}")
    if macro == "STICKY_SALAH_VERSION":
        network = (root / "src/services/network_location.cpp").read_text()
        if 'config.user_agent = "Sticky-Salah/" STICKY_SALAH_VERSION;' not in network:
            raise ValueError("HTTP User-Agent must derive its version from STICKY_SALAH_VERSION")
    return version


if __name__ == "__main__":
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    try:
        version = check_versions(root)
    except ValueError as error:
        print(f"[version-check] FAIL: {error}", file=sys.stderr)
        sys.exit(1)
    print(f"[version-check] PASS runtime and ESP-IDF descriptor: {version}")
