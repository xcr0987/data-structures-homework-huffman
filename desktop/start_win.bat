@echo off
REM 【修改】仅启动桌面GUI，不编译C。
cd /d "%~dp0"
if not exist .venv (
    py -3 -m venv .venv
    if errorlevel 1 exit /b 1
    .venv\Scripts\python.exe -m pip install -r requirements.txt
    if errorlevel 1 exit /b 1
)
.venv\Scripts\pythonw.exe app.py
