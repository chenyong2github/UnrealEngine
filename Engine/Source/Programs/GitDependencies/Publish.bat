@echo off
call ..\..\..\Build\BatchFiles\GetDotnetPath.bat
del /s /q "..\..\..\Binaries\DotNET\GitDependencies\*"

echo.
echo Building for win-x64...
"%DOTNET_ROOT%\dotnet" publish GitDependencies.csproj -r win-x64 --output "..\..\..\Binaries\DotNET\GitDependencies\win-x64" --nologo
if errorlevel 1 goto :eof

echo.
echo Building for osx-x64...
"%DOTNET_ROOT%\dotnet" publish GitDependencies.csproj -r osx-x64 --output "..\..\..\Binaries\DotNET\GitDependencies\osx-x64" --nologo
if errorlevel 1 goto :eof

echo.
echo Building for linux-x64...
"%DOTNET_ROOT%\dotnet" publish GitDependencies.csproj -r linux-x64 --output "..\..\..\Binaries\DotNET\GitDependencies\linux-x64" --nologo
if errorlevel 1 goto :eof
