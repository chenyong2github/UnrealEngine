@echo off
setlocal
pushd "%~dp0"
call ..\..\..\Build\BatchFiles\GetDotnetPath.bat
del /s /q "..\..\..\Binaries\DotNET\GitDependencies\*"

echo.
echo Building portable binaries...
rmdir /s /q bin
rmdir /s /q obj
"%DOTNET_ROOT%\dotnet" publish GitDependencies.csproj -p:PublishProfile=FolderProfile --nologo
if errorlevel 1 goto :eof

endlocal
