#!/usr/bin/env python3
"""Fetch the prebuilt OpenXR loader .so this project's AndroidXROpenXR plugin needs.

Plugins/AndroidXROpenXR/Source/AndroidXROpenXRLoader/Libs/Android/arm64-v8a/
libandroidxr_openxr_loader.so is gitignored (blanket *.so rule) and must be
fetched once per clone before packaging for Android -- without it,
BuildCookRun fails at the final staging step with:
    Failed to copy .../libandroidxr_openxr_loader.so ... because it doesn't exist.

Source + exact version are pinned in Libs/Android/Version.txt. Run this once
after cloning, or any time that file is missing.
"""
import io
import pathlib
import sys
import urllib.request
import zipfile

VERSION = "1.0.27"
URL = (
    "https://github.com/KhronosGroup/OpenXR-SDK-Source/releases/download/"
    f"release-{VERSION}/openxr_loader_for_android-{VERSION}.aar"
)
AAR_INTERNAL_PATH = "prefab/modules/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so"

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = (
    REPO_ROOT
    / "Plugins/AndroidXROpenXR/Source/AndroidXROpenXRLoader/Libs/Android/arm64-v8a"
    / "libandroidxr_openxr_loader.so"
)


def main():
    if DEST.exists():
        print(f"Already present: {DEST}")
        return 0

    print(f"Downloading {URL} ...")
    with urllib.request.urlopen(URL, timeout=60) as r:
        data = r.read()

    with zipfile.ZipFile(io.BytesIO(data)) as z:
        so_bytes = z.read(AAR_INTERNAL_PATH)

    DEST.parent.mkdir(parents=True, exist_ok=True)
    DEST.write_bytes(so_bytes)
    print(f"Wrote {DEST} ({len(so_bytes)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
