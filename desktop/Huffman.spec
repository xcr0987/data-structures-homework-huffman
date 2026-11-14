# 【修改】Windows目录式交付；只打包GUI，不编译或改变C程序。
from importlib.metadata import distribution
from pathlib import Path
import shutil
import sys

if sys.platform != "win32":
    raise SystemExit("Windows交付包必须在Windows上构建。")

root = Path(SPECPATH)
engine = root.parent / "huffman.exe"
if not engine.is_file():
    raise SystemExit("请先把已编译、可运行的huffman.exe放到第二次验收目录。")

# 【修改】保留Python、PySide6和打包工具随附的许可证原文。
datas = [(str(root / "licenses"), "licenses")]
for name in ("PySide6_Essentials", "shiboken6", "PyInstaller"):
    package = distribution(name)
    for file in package.files or ():
        if "licenses" in file.parts:
            relative = Path(*file.parts[file.parts.index("licenses") + 1:])
            datas.append((str(package.locate_file(file)), str(Path("licenses") / name / relative.parent)))
python_license = Path(sys.base_prefix) / "LICENSE.txt"
if python_license.is_file():
    datas.append((str(python_license), "licenses/Python"))

a = Analysis([str(root / "app.py")], pathex=[str(root)], binaries=[], datas=datas,
             hiddenimports=[], hookspath=[], runtime_hooks=[], excludes=[])
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name="HuffmanGUI",
          debug=False, strip=False, upx=False, console=False)
collect = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name="HuffmanDesktop")

# 【修改】引擎与GUI并列，支持整个目录移动；带上引擎旁已有的运行库。
output = Path(DISTPATH) / "HuffmanDesktop"
for path in [engine, *root.parent.glob("*.dll")]:
    shutil.copy2(path, output / path.name)
for name in ("README.md", "THIRD_PARTY.md"):
    shutil.copy2(root / name, output / name)
