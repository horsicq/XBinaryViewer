@echo off
REM Build in system temporary folder

call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86

REM Create temporary build directory
set BUILD_DIR=%TEMP%\xbinaryviewer-build-%RANDOM%
set PROJECT_DIR=%~dp0..

echo Building in temporary directory: %BUILD_DIR%

mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"
cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_PREFIX_PATH="C:\Qt5.6.3\5.6.3\msvc2013" -G "NMake Makefiles" "%PROJECT_DIR%"
nmake
cpack

REM Copy packages back to project
if not exist "%PROJECT_DIR%\packages" mkdir "%PROJECT_DIR%\packages"
xcopy /y /E "%BUILD_DIR%\packages\*" "%PROJECT_DIR%\packages\" 2>nul

REM Cleanup
cd /d "%PROJECT_DIR%"
rmdir /s /q "%BUILD_DIR%"
echo Cleaned up temporary build directory