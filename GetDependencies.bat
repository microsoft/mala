@echo off

REM very naive dependency management, need a better one.

pushd "%~dp0"

IF EXIST ".\extern" GOTO HAS_EXTERN

mkdir extern

:HAS_EXTERN

cd extern

IF EXIST ".\GSL" GOTO GSL_PULLED

echo "pulling GSL..."

git clone --depth=1 https://github.com/Microsoft/GSL.git

:GSL_PULLED



:END
popd