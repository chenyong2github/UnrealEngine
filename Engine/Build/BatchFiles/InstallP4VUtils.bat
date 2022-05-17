setlocal

set LIBPATH=%LOCALAPPDATA%\Unreal Engine\P4VUtils

echo Copying P4VUtils files to %LIBPATH%...
mkdir "%LIBPATH%"

copy ../../../Extras/P4VUtils/Binaries/* "%LIBPATH%"
copy ../../../Extras/P4VUtils/P4VUtils.ini "%LIBPATH%"
copy ../../../Restricted/NotForLicensees/* "%LIBPATH%" 2> NUL

echo Installing P4VUtils into p4v...
dotnet "%LIBPATH%\P4VUtils.dll" install
