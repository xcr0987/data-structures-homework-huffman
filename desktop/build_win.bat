@echo off
REM 【修改】制作Windows目录式交付包；使用已经编译好的huffman.exe。
cd /d "%~dp0"
if not exist .venv (
    py -3 -m venv .venv
    if errorlevel 1 exit /b 1
)
.venv\Scripts\python.exe -m pip install -r requirements-build.txt
if errorlevel 1 exit /b 1
.venv\Scripts\python.exe -m PyInstaller --noconfirm Huffman.spec
if errorlevel 1 exit /b 1
echo dist\HuffmanDesktop\HuffmanGUI.exe
