@echo off
setlocal ENABLEDELAYEDEXPANSION

set _switchboardDir=%~dp0
REM pushd and %CD% are used to normalize the relative path to a shorter absolute path.
pushd "%~dp0..\..\..\..\.."
set _engineDir=%CD%
set _enginePythonPlatformDir=%_engineDir%\Binaries\ThirdParty\Python3\Win64
set _pyVenvDir=%_engineDir%\Extras\ThirdPartyNotUE\SwitchboardThirdParty\Python
popd

call:main

endlocal
goto:eof

::------------------------------------------------------------------------------
:main

if not exist "%_pyVenvDir%\Scripts\pythonw.exe" (
    echo Performing Switchboard first-time setup (using default path for Python virtual environment^)
    call "%_enginePythonPlatformDir%\python.exe" "%~dp0\sb_setup.py" install --venv-dir="%_pyVenvDir%"
    if !ERRORLEVEL! NEQ 0 (
        echo Installation failed with non-zero exit code^^^!
        pause
        exit /B !ERRORLEVEL!
    )
)

call:start_sb

goto:eof

::------------------------------------------------------------------------------
:start_sb

set PYTHONPATH=%_switchboardDir%;%PYTHONPATH%
start "Switchboard" "%_pyVenvDir%\Scripts\pythonw.exe" -m switchboard
if %ERRORLEVEL% NEQ 0 (
    echo Failed to launch Switchboard^!
    pause
)
