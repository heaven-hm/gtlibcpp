@echo off

set FILENAME=%1
set DEBUG_OPTION=%2

if "%FILENAME%"=="" (
    echo Usage: CppRunner.bat Filename.cpp [--debug]
    exit /b 1
)

if not exist "%FILENAME%" (
    echo Error: File not found: %FILENAME%
    exit /b 1
)

set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..
set GTLIBC_INC=%REPO_ROOT%\libs\gtlibc\include
set GTLIBC_SRC=%REPO_ROOT%\libs\gtlibc\src
set GTLIBCPP_INC=%REPO_ROOT%\include

g++ -std=c++17 -w -DGT_USE_CE_PARSER -I"%GTLIBC_INC%" -I"%GTLIBCPP_INC%" -o "%~n1.exe" "%GTLIBC_SRC%\GTLibc.cpp" "%GTLIBC_SRC%\CEParser.cpp" "%FILENAME%"

if %ERRORLEVEL% neq 0 (
    echo Error: Compilation failed for file %FILENAME%
    exit /b 1
)

if /i "%DEBUG_OPTION%"=="--debug" (
    "%~n1.exe" 2>&1
) else (
    "%~n1.exe"
)

exit /b 0
