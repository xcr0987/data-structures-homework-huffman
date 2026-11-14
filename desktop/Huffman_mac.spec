# 【修改】macOS独立.app，包含已有C引擎，不编译C，不依赖系统Python。
from importlib.metadata import distribution
from pathlib import Path
import sys

if sys.platform != "darwin":
    raise SystemExit("macOS应用必须在Mac上构建。")

root = Path(SPECPATH)
engine = root.parent / "huffman"
if not engine.is_file():
    raise SystemExit("请先把已编译、可运行的huffman放到第二次验收目录。")

# 【修改】沿用Windows包的许可证收集，附带说明文档。
datas = [(str(root / "licenses"), "licenses"),
         (str(root / "README.md"), "."), (str(root / "THIRD_PARTY.md"), ".")]
for name in ("PySide6_Essentials", "shiboken6", "PyInstaller"):
    package = distribution(name)
    for file in package.files or ():
        if "licenses" in file.parts:
            relative = Path(*file.parts[file.parts.index("licenses") + 1:])
            datas.append((str(package.locate_file(file)), str(Path("licenses") / name / relative.parent)))
python_license = Path(sys.base_prefix) / "LICENSE.txt"
if python_license.is_file():
    datas.append((str(python_license), "licenses/Python"))

a = Analysis([str(root / "app.py")], pathex=[str(root)], binaries=[(str(engine), ".")],
             datas=datas, hiddenimports=[], hookspath=[], runtime_hooks=[], excludes=[])
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name="HuffmanGUI",
          debug=False, strip=False, upx=False, console=False)
# 【修改】直接构建应用，不额外生成一份重复的运行目录。
app = BUNDLE(exe, a.binaries, a.datas, name="HuffmanGUI.app",
             bundle_identifier="org.huffman.desktop",
             info_plist={"CFBundleDisplayName": "哈夫曼编码工具", "NSHighResolutionCapable": True})
