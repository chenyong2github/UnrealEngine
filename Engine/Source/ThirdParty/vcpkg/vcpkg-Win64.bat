@echo off

setlocal

:: this is a tag in the vcpkg repository
set VCPKG_VERSION=2021.05.12

:: enable manifest mode
set VCPKG_FEATURE_FLAGS=manifests

:: the triplet to build
set VCPKG_TRIPLETS=x64-windows-static-md-v142;x64-windows-static-v142

:: the Unreal platform
set UE_PLATFORM=Win64

pushd %~dp0

echo:
echo === Checking out vcpkg to vcpkg ===
git clone https://github.com/microsoft/vcpkg.git --depth 1 --branch %VCPKG_VERSION% vcpkg-%UE_PLATFORM%

echo:
echo === Bootstrapping vcpkg ===
call vcpkg-%UE_PLATFORM%\bootstrap-vcpkg.bat -disableMetrics

echo:
echo === Making %UE_PLATFORM% artifacts writeable ===
attrib -R .\%UE_PLATFORM%\*.* /s

echo:
echo === Running vcpkg in manifest mode ===
FOR %%T IN (%VCPKG_TRIPLETS%) DO (
    mkdir .\%UE_PLATFORM%\%%T
    copy /Y .\vcpkg.json .\%UE_PLATFORM%\%%T\vcpkg.json

    vcpkg-%UE_PLATFORM%\vcpkg.exe install ^
        --overlay-ports=.\overlay-ports ^
        --overlay-triplets=.\overlay-triplets ^
        --x-manifest-root=.\%UE_PLATFORM%\%%T ^
        --x-packages-root=.\%UE_PLATFORM%\%%T ^
        --triplet=%%T
)

echo:
echo === Reconciling %UE_PLATFORM% artifacts ===
p4 reconcile .\%UE_PLATFORM%\...

popd

endlocal