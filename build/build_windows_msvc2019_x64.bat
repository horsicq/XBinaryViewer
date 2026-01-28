@echo off
REM Build in system temporary folder

IF EXIST "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
	call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64
) ELSE (
	call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
)

REM Create temporary build directory
set BUILD_DIR=%TEMP%\xbinaryviewer-build-%RANDOM%
set PROJECT_DIR=%~dp0..

echo Building in temporary directory: %BUILD_DIR%

mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"
cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64" -G "NMake Makefiles" "%PROJECT_DIR%"
nmake
cpack

REM Copy packages back to project
if not exist "%PROJECT_DIR%\packages" mkdir "%PROJECT_DIR%\packages"
xcopy /y /E "%BUILD_DIR%\packages\*" "%PROJECT_DIR%\packages\" 2>nul

REM Cleanup
cd /d "%PROJECT_DIR%"
rmdir /s /q "%BUILD_DIR%"
echo Cleaned up temporary build directory