#!/bin/sh
# 【修改】打包已有Mac引擎和桌面窗口，不编译C；产物放在_mac_dist。
cd "$(dirname "$0")" || exit 1
if [ ! -d .venv ]; then
    python3 -m venv .venv || exit 1
fi
.venv/bin/python -m pip --disable-pip-version-check install -r requirements-build.txt || exit 1
export PYINSTALLER_CONFIG_DIR="$PWD/_mac_build/cache"
.venv/bin/python -m PyInstaller --noconfirm --distpath _mac_dist --workpath _mac_build Huffman_mac.spec || exit 1
/usr/bin/ditto -c -k --sequesterRsrc --keepParent _mac_dist/HuffmanGUI.app _mac_dist/HuffmanGUI-mac.zip || exit 1
printf '%s\n' "$PWD/_mac_dist/HuffmanGUI.app" "$PWD/_mac_dist/HuffmanGUI-mac.zip"
