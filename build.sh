#!/bin/bash
# ESP-IDF build wrapper for Git Bash
# Usage:
#   ./build.sh [target]                    # use default IDF path
#   ./build.sh -i /path/to/esp-idf [target] # specify IDF path
# Targets: build, flash, clean, erase-flash, menuconfig, ...
#
# Options:
#   -i, --idf PATH   ESP-IDF root directory (default: D:\Espressif\frameworks\esp-idf-v5.5.4)
#   -p, --pyenv PATH  Python venv path      (default: D:\Espressif\python_env\idf5.5_py3.11_env)

# Defaults
IDF_PATH="D:\\Espressif\\frameworks\\esp-idf-v5.5.4"
PYENV_PATH="D:\\Espressif\\python_env\\idf5.5_py3.11_env"

# Parse options
while [[ $# -gt 0 ]]; do
  case "$1" in
    -i|--idf)   IDF_PATH="$2"; shift 2 ;;
    -p|--pyenv) PYENV_PATH="$2"; shift 2 ;;
    -*) echo "Usage: $0 [-i IDF_PATH] [-p PYENV_PATH] [target]"; exit 1 ;;
    *) break ;;
  esac
done
TARGET="${1:-build}"

DIR="$(cygpath -w "$(cd "$(dirname "$0")" && pwd)")"

powershell.exe -NoProfile -Command '
  $env:MSYSTEM="";
  $env:IDF_PYTHON_ENV_PATH="'"$PYENV_PATH"'";
  . "'"$IDF_PATH"'\export.ps1" 2>$null;
  Set-Location "'"$DIR"'";
  idf.py '"$TARGET"'
'
