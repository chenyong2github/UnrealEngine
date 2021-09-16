@echo off
setlocal

REM pushd and %CD% are used to normalize the relative path to a shorter absolute path.
pushd "%~dp0..\..\..\..\.."
set _engineDir=%CD%
set _enginePythonPlatformDir=%_engineDir%\Binaries\ThirdParty\Python3\Win64
set _pyVenvDir=%_engineDir%\Extras\ThirdPartyNotUE\SwitchboardThirdParty\Python
set _cwrsyncDir=%_engineDir%\Extras\ThirdPartyNotUE\cwrsync
set _sbCwrsyncDir=%_engineDir%\Extras\ThirdPartyNotUE\SwitchboardThirdParty\cwrsync
popd

call:main

endlocal
goto:eof

::------------------------------------------------------------------------------
:main

REM This provides a transition from standalone installations of Python for
REM Switchboard to the venv-based setup using the engine version of Python.
if exist "%_pyVenvDir%" (
    if not exist "%_pyVenvDir%\Scripts" (
        rd /q /s "%_pyVenvDir%" 2>nul
    )
)

if not exist "%_pyVenvDir%" (
    call:setup_python_venv
)

REM We make our own working copy of cwrsync to scope our modifications to etc/fstab.
if not exist "%_sbCwrsyncDir%\bin\rsync.exe" (
    robocopy /NP /S "%_cwrsyncDir%" "%_sbCwrsyncDir%" 1>"%_sbCwrsyncDir%\provision.log" 2>&1
)

call:start_sb

goto:eof

::------------------------------------------------------------------------------
:setup_python_venv

1>nul 2>nul (
    mkdir "%_pyVenvDir%"
)

1>"%_pyVenvDir%\provision.log" 2>&1 (
    set prompt=-$s
    echo on
    call:setup_python_venv_impl
    echo off
)

echo.
goto:eof

::------------------------------------------------------------------------------
:setup_python_venv_impl

pushd "%_pyVenvDir%"
call:echo "Working path; %CD%"

call:echo "1/5 : Setting up Python Virtual Environment"
call "%_enginePythonPlatformDir%\python.exe" -m venv "%_pyVenvDir%"

call "%_pyVenvDir%\Scripts\activate"

call:echo "2/5 : Installing PySide2"
python.exe -m pip install -Iv pyside2==5.15.0

call:echo "3/5 : Installing python-osc"
python.exe -m pip install -Iv python-osc==1.7.4

call:echo "4/5 : Installing requests"
python.exe -m pip install -Iv requests==2.24.0

call:echo "5/5 : Installing six"
python.exe -m pip install -Iv six==1.15.0

call deactivate

popd
goto:eof

::------------------------------------------------------------------------------
:echo
1>con echo %~1
goto:eof

::------------------------------------------------------------------------------
:start_sb

call "%_pyVenvDir%\Scripts\activate"
start "Switchboard" /D "%~dp0" pythonw.exe -m switchboard
