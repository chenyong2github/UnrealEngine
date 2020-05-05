@echo off

REM Install PS4 visualizer if the SDK and installation file are present
if exist "%~dp0Engine\Extras\VisualStudioDebugging\PS4\InstallPS4Visualizer.bat" (
  call "%~dp0Engine\Extras\VisualStudioDebugging\PS4\InstallPS4Visualizer.bat"
)

if not exist "%~dp0Engine\Build\BatchFiles\GenerateProjectFiles.bat" goto Error_BatchFileInWrongLocation
call "%~dp0Engine\Build\BatchFiles\GenerateProjectFiles.bat" %*
exit /B %ERRORLEVEL%

:Error_BatchFileInWrongLocation
echo GenerateProjectFiles ERROR: The batch file does not appear to be located in the root UE4 directory.  This script must be run from within that directory.
pause
exit /B 1
