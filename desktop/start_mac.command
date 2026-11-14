#!/bin/sh
# 【修改】仅启动桌面GUI；C程序应提前编译，不在这里构建。
cd "$(dirname "$0")" || exit 1
if [ ! -d .venv ]; then
    python3 -m venv .venv || exit 1
    .venv/bin/python -m pip install -r requirements.txt || exit 1
fi
exec .venv/bin/python app.py
