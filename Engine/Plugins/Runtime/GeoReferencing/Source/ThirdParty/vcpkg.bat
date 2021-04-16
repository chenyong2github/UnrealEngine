@echo off

setlocal

:: this is a tag in the vcpkg repository
set VCPKG_VERSION=2020.11-1

:: enable manifest mode
set VCPKG_FEATURE_FLAGS=manifests

echo:
echo === Checking out vcpkg to %~dp0vcpkg ===
git clone https://github.com/microsoft/vcpkg.git --depth 1 --branch %VCPKG_VERSION% %~dp0vcpkg

echo:
echo === Bootstrapping vcpkg ===
call %~dp0vcpkg\bootstrap-vcpkg.bat

echo:
echo === Making vcpkg_installed artifacts writeable ===
attrib -R %~dp0vcpkg_installed\x64-windows-static-md\*.* /s

echo:
echo === Running vcpkg in manifest mode ===
%~dp0vcpkg\vcpkg.exe install --x-manifest-root=%~dp0 --triplet=x64-windows-static-md

echo:
echo === Reconciling vcpkg_installed artifacts ===
for /f %%f in ("%~dp0vcpkg_installed\x64-windows-static-md") do p4 reconcile %%~ff\...

echo:
echo === Refreshing PROJ data files ===

:: destroy the target
attrib -r %~dp0..\..\Resources\PROJ\*.* /s
rmdir /s /q %~dp0..\..\Resources\PROJ

:: copy the files
robocopy /MIR /MT %~dp0vcpkg_installed\x64-windows-static-md\share\proj4 %~dp0..\..\Resources\PROJ

:: delete some extra stuff
del %~dp0..\..\Resources\PROJ\*.cmake
del %~dp0..\..\Resources\PROJ\vcpkg*.*

:: reconcile in p4 (for /f will handle relative paths that p4 can't handle)
for /f %%f in ("%~dp0..\..\Resources\PROJ") do p4 reconcile %%~ff\...

endlocal