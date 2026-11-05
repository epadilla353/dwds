@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 goto nocmake
where git >nul 2>nul
if errorlevel 1 goto nogit

echo === Configuring (downloads JUCE the first time, please wait) ===
cmake -S . -B build -A x64
if errorlevel 1 goto fail

echo === Building Release ===
cmake --build build --config Release --parallel
if errorlevel 1 goto fail

if exist dist rmdir /s /q dist
mkdir dist
xcopy /e /i /y "build\SESEQ_artefacts\Release\VST3\SES EQ.vst3" "dist\SES EQ.vst3" >nul
copy /y "build\SESEQ_artefacts\Release\Standalone\SES EQ.exe" "dist\SES EQ.exe" >nul

echo.
echo Built OK. Files are in the "dist" folder:
echo   SES EQ.vst3  (the plugin for your DAW)
echo   SES EQ.exe   (standalone app for testing)
echo.
choice /m "Install the VST3 into the system VST3 folder now (needs Administrator)"
if errorlevel 2 goto done
xcopy /e /i /y "dist\SES EQ.vst3" "%CommonProgramFiles%\VST3\SES EQ.vst3"
if errorlevel 1 echo Copy failed - run this script as Administrator, or copy dist\SES EQ.vst3 into %CommonProgramFiles%\VST3 yourself.
goto done

:nocmake
echo CMake not found. Install it from https://cmake.org/download/ (tick "Add to PATH"), then run again.
goto fail
:nogit
echo Git not found. Install it from https://git-scm.com/download/win, then run again.
goto fail
:fail
echo.
echo Build failed. You need Visual Studio 2022 (or Build Tools) with the "Desktop development with C++" workload.
:done
pause
