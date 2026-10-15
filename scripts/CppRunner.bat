@echo off

set FILENAME=%1
set DEBUG_OPTION=%2

if "%FILENAME%"=="" (
    echo Usage: compile_and_run.bat Filename.cpp [--debug]
    exit /b 1
)

if not exist "%FILENAME%" (
    echo Error: File not found: %FILENAME%
    exit /b 1
)

g++ -std=c++17 -w -DGT_USE_CE_PARSER -o "%~n1.exe" GTLibc.cpp CEParser.cpp %FILENAME%

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
