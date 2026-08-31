@echo off
setlocal EnableExtensions

if /i "%~1"=="--run-analysis" goto :run_analysis

set "ANALYSIS_LOG=%~dp0Original_Analysis.log"

call "%~f0" --run-analysis > "%ANALYSIS_LOG%" 2>&1
set "ANALYSIS_EXIT_CODE=%ERRORLEVEL%"

type "%ANALYSIS_LOG%"
echo.
echo Results saved:
echo   %ANALYSIS_LOG%
echo.
pause
exit /b %ANALYSIS_EXIT_CODE%

:run_analysis
title IceCream - Analyze Original VST2

set "PROJECT_ROOT=%~dp0"
set "SOURCE_DIR=%PROJECT_ROOT%source\ReferenceProbe"
set "BUILD_DIR=%PROJECT_ROOT%build-reference-probe"
set "CMAKE_EXE=%PROJECT_ROOT%..\_Tools\cmake\_4.4.2\bin\cmake.exe"
set "ORIGINAL_DLL=%PROJECT_ROOT%Icecream.dll"
set "SAW_DATA=%PROJECT_ROOT%saw2KtableSM.dat"
set "TRIANGLE_DATA=%PROJECT_ROOT%tri2KtableSM.dat"
set "PROBE_EXE=%BUILD_DIR%\Release\IceCreamReferenceProbe.exe"

echo.
echo IceCream Original VST2 Reference Analysis - Stage 40.3
echo ======================================================
echo.

echo [1/4] Checking required files...

if not exist "%CMAKE_EXE%" goto :missing_cmake
if not exist "%SOURCE_DIR%\CMakeLists.txt" goto :missing_source
if not exist "%SOURCE_DIR%\ReferenceProbe.cpp" goto :missing_source
if not exist "%ORIGINAL_DLL%" goto :missing_dll
if not exist "%SAW_DATA%" goto :missing_saw
if not exist "%TRIANGLE_DATA%" goto :missing_triangle

echo       PASS
echo.
echo [2/4] Configuring the isolated Win32 reference probe...

"%CMAKE_EXE%" -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 18 2026" -A Win32
if errorlevel 1 goto :configure_failed

echo       PASS
echo.
echo [3/4] Building the Win32 reference probe...

"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release --target IceCreamReferenceProbe --clean-first --parallel
if errorlevel 1 goto :build_failed

if not exist "%PROBE_EXE%" goto :probe_not_found

echo       PASS
echo.
echo [4/4] Reading metadata, state, gain, BITRATE, envelopes, sequencing, and audio behaviour...
echo.

pushd "%PROJECT_ROOT%"
"%PROBE_EXE%" "%ORIGINAL_DLL%"
set "PROBE_EXIT_CODE=%ERRORLEVEL%"
popd

if not "%PROBE_EXIT_CODE%"=="0" goto :probe_failed

echo.
echo ANALYSIS RUN SUMMARY
echo   - PASS: Original VST2 and data files found
echo   - PASS: Win32 reference probe configured
echo   - PASS: Win32 reference probe compiled
echo   - PASS: Original plug-in metadata captured
echo   - PASS: Complete per-program chunks captured for sequencer recovery and state verification
echo   - PASS: All factory-program EQ responses measured twice for stability
echo   - PASS: Octave, fine-frequency, and waveform audio measurements captured
echo   - PASS: OSC2 FREQ movement measured in cents against its default
echo   - PASS: Filter type, cutoff, resonance, and tracking spectra captured
echo   - PASS: Baseline pitch movement and OSC2 FREQ/HARMONIX interaction captured
echo   - PASS: Main volume, oscillator volume, and MIDI velocity curves captured
echo   - PASS: BITRATE direction, spectrum, and oscillator switch mappings captured
echo   - PASS: Sustain response and All Star amplifier-envelope timing captured
echo   - PASS: All Star pitch and filter sequencer movement captured separately
echo.
echo No project, plug-in, preset, template, or system file was modified.
exit /b 0

:missing_cmake
echo.
echo ANALYSIS FAILED
echo   - FAIL: CMake 4.4.2 was not found
echo.
echo Expected:
echo   %CMAKE_EXE%
goto :failed

:missing_source
echo.
echo ANALYSIS FAILED
echo   - FAIL: Reference probe source files were not found
echo.
echo Expected folder:
echo   %SOURCE_DIR%
goto :failed

:missing_dll
echo.
echo ANALYSIS FAILED
echo   - FAIL: Icecream.dll was not found
echo.
echo Expected:
echo   %ORIGINAL_DLL%
goto :failed

:missing_saw
echo.
echo ANALYSIS FAILED
echo   - FAIL: saw2KtableSM.dat was not found beside Icecream.dll
goto :failed

:missing_triangle
echo.
echo ANALYSIS FAILED
echo   - FAIL: tri2KtableSM.dat was not found beside Icecream.dll
goto :failed

:configure_failed
echo.
echo ANALYSIS FAILED
echo   - PASS: Required files found
echo   - FAIL: CMake could not configure the Win32 probe
goto :failed

:build_failed
echo.
echo ANALYSIS FAILED
echo   - PASS: Win32 probe configuration completed
echo   - FAIL: Win32 probe compilation failed
goto :failed

:probe_not_found
echo.
echo ANALYSIS FAILED
echo   - PASS: Compilation command completed
echo   - FAIL: IceCreamReferenceProbe.exe was not found
echo.
echo Expected:
echo   %PROBE_EXE%
goto :failed

:probe_failed
echo.
echo ANALYSIS FAILED
echo   - PASS: Win32 reference probe compiled
echo   - FAIL: The original VST2 could not be analyzed safely
goto :failed

:failed
echo.
echo The full result is retained in Original_Analysis.log.
exit /b 1
