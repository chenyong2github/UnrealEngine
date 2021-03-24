@echo off
setlocal
call:main "%~dp0.thirdparty\python"
endlocal
goto:eof

::------------------------------------------------------------------------------
:main
set _pyver=3.7.7
set _pytag=37
set _destdir=%~f1\%_pyver%

if exist "%_destdir%" (
    call:start_sb
    goto:eof
)

1>nul 2>nul (
    rd "%_destdir%\..\current"
)

call:get_python "%_destdir%"

1>nul 2>nul (
    mklink /j "%_destdir%\..\current" "%_destdir%"
)

call:start_sb

goto:eof

::------------------------------------------------------------------------------
:get_python
set _workdir=%~1~
for /f "delims=, tokens=2" %%d in ('tasklist.exe /fo csv /fi "imagename eq cmd.exe" /nh') do (
    set _workdir=%~1_%%~d
)

1>nul 2>nul (
    mkdir "%_workdir%"
)

1>"%_workdir%\provision.log" 2>&1 (
    set prompt=-$s
    echo on
    call:get_python_impl "%_workdir%"
    echo off
)

move "%_workdir%" "%~1" 1>nul 2>nul
if not exist "%~1" (
    call:on_error "Failed finalising Python provision"
)

echo.
goto:eof

::------------------------------------------------------------------------------
:get_python_impl
rd /q /s "%~1" 2>nul
if exist "%~1" call:on_error "Unable to remove directory '%~1'"
md "%~1"
pushd "%~1"

call:echo "Working path; %~1"
call:echo "1/6 : Getting Python %_pyver%"
call:get_url https://www.python.org/ftp/python/%_pyver%/python-%_pyver%-embed-amd64.zip
call:unzip python-%_pyver%-embed-amd64.zip .

del *._pth
call:unzip python%_pytag%.zip Lib

md DLLs
move *.pyd DLLs
move lib*.dll DLLs
move sq*.dll DLLs

call:echo "2/6 : Adding Pip"
call:get_url https://bootstrap.pypa.io/get-pip.py
.\python.exe get-pip.py
del get-pip.py

call:echo "3/6 : Installing Pyside2"
.\python.exe -m pip install -Iv pyside2==5.15.0

call:echo "4/6 : Installing python-osc"
.\python.exe -m pip install -Iv python-osc==1.7.4

call:echo "5/6 : Installing requests"
.\python.exe -m pip install -Iv requests==2.24.0

call:echo "6/6 : Installing six"
.\python.exe -m pip install -Iv six==1.15.0

popd
goto:eof

::------------------------------------------------------------------------------
:powershell
set _psscript=%*
powershell.exe -NoProfile -NoLogo -Command %_psscript:"=\"%
set _psscript=
goto:eof

::------------------------------------------------------------------------------
:get_url
call:powershell ^
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; ^
    (New-Object System.Net.WebClient).DownloadFile("%~1", "%~nx1")
call:on_error "Failed to get url '%~1'"
goto:eof

::------------------------------------------------------------------------------
:unzip
call:powershell ^
    add-type -assembly "System.IO.Compression.FileSystem"; ^
    [io.compression.zipfile]::extracttodirectory("%~1","%~2")
call:on_error "Failed to unzip '%~1'"
del %1
goto:eof

::------------------------------------------------------------------------------
:echo
1>con echo %~1
goto:eof

::------------------------------------------------------------------------------
:on_error
if not %errorlevel%==0 1>&2 (
    echo ERROR: %~1
    1>con (
        :: timeout's printing is buggy when 1>con is used :(
        echo ERROR: %~1
        timeout /T 5
    )
    exit 1
)
goto:eof

::------------------------------------------------------------------------------
:start_sb
start .\.thirdparty\python\current\pythonw.exe -m switchboard