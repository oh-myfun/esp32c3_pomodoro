#!/bin/bash
# ESP-IDF build wrapper for Git Bash
# Usage: ./build.sh [target]  (build, flash, clean, erase-flash, menuconfig, ...)
# Paths: only ESP-IDF root and Python venv are specified;
#         cmake/ninja/toolchain versions are resolved by export.ps1

DIR="$(cygpath -w "$(cd "$(dirname "$0")" && pwd)")"
TARGET="${1:-build}"

powershell.exe -NoProfile -Command '
  $env:MSYSTEM="";
  $env:IDF_PYTHON_ENV_PATH="D:\Espressif\python_env\idf5.5_py3.11_env";
  . "D:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1" 2>$null;
  Set-Location "'"$DIR"'";
  idf.py '"$TARGET"'
'
