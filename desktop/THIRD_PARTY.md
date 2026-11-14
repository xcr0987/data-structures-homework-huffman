<!-- 【修改】桌面版第三方组件来源与交付目录许可证位置。 -->
# 第三方组件

- Python：Python Software Foundation，https://www.python.org/ 。Windows包的Python许可证保存在 `_internal/licenses/Python/`。
- PySide6、Shiboken6、Qt：Qt Project / The Qt Company，https://www.qt.io/qt-for-python 。使用官方PySide6 Essentials预编译包；官方v6.10.1源码仓库的LICENSES目录原文保存在 `licenses/PySide6/`，Windows包将其放到 `_internal/licenses/PySide6/`。来源：https://github.com/pyside/pyside-setup/tree/v6.10.1/LICENSES 。
- PyInstaller：https://pyinstaller.org/ 。仅用于构建交付包，原始许可证保存在 `_internal/licenses/PyInstaller/`。

Python开发依赖和打包依赖分别记录在 `requirements.txt`、`requirements-build.txt`。交付采用可分离的目录结构，第三方运行库保留为独立文件。

`huffman.exe` 为本项目已有的C程序；若其构建依赖其他DLL，应同时保留这些组件随附的许可证与说明。
