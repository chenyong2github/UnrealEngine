@echo off
setlocal

set ROOT_LOCATION=%cd%

set THIRD_PARTY_LOCATION=%ROOT_LOCATION%\..
set ILMBASE_ROOT=%THIRD_PARTY_LOCATION%\openexr\Deploy\OpenEXR-2.3.0\OpenEXR

set HDF5_ROOT=%ROOT_LOCATION%\Intermediate\hdf5\Install
if not exist %HDF5_ROOT% (
    echo The HDF5 installation directory does not exist:
    echo     %HDF5_ROOT%
    echo Please run HDF5VS.bat before running AlembicVS.bat
    exit /B 1
)

set SOURCE_LOCATION=%ROOT_LOCATION%\alembic

set BUILD_LOCATION=%ROOT_LOCATION%\Intermediate\alembic
set INSTALL_LOCATION=%BUILD_LOCATION%\Install

set DEPLOY_LOCATION=%ROOT_LOCATION%\AlembicDeploy
set DEPLOY_INCLUDE_LOCATION=%DEPLOY_LOCATION%\include
set DEPLOY_LIB_LOCATION=%DEPLOY_LOCATION%\VS2015\x64\lib

if exist %BUILD_LOCATION% (
    rmdir %BUILD_LOCATION% /S /Q)
if exist %DEPLOY_INCLUDE_LOCATION%\Alembic (
    rmdir %DEPLOY_INCLUDE_LOCATION%\Alembic /S /Q)
if exist %DEPLOY_LIB_LOCATION%\*Alembic* (
    del %DEPLOY_LIB_LOCATION%\*Alembic* /Q)

mkdir %BUILD_LOCATION%
pushd %BUILD_LOCATION%

echo Configuring build for Alembic...
cmake -G "Visual Studio 16 2019" %SOURCE_LOCATION%^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_LOCATION%"^
    -DZLIB_INCLUDE_DIR=%THIRD_PARTY_LOCATION%\zlib\v1.2.8\include\Win64\VS2015\^
    -DZLIB_LIBRARY=%THIRD_PARTY_LOCATION%\zlib\v1.2.8\lib\Win64\VS2015\^
    -DALEMBIC_SHARED_LIBS=OFF^
    -DUSE_TESTS=OFF^
    -DUSE_BINARIES=OFF^
    -DUSE_HDF5=ON^
    -DALEMBIC_ILMBASE_LINK_STATIC=ON^
    -DUSE_STATIC_HDF5=OFF
if %errorlevel% neq 0 exit /B %errorlevel%

echo Building Alembic for Debug...
cmake --build . --config Debug -j8
if %errorlevel% neq 0 exit /B %errorlevel%

echo Installing Alembic for Debug...
cmake --install . --config Debug
if %errorlevel% neq 0 exit /B %errorlevel%

echo Building Alembic for Release...
cmake --build . --config Release -j8
if %errorlevel% neq 0 exit /B %errorlevel%

echo Installing Alembic for Release...
cmake --install . --config Release
if %errorlevel% neq 0 exit /B %errorlevel%

popd

echo Deploying Alembic...
xcopy "%INSTALL_LOCATION%\include" "%DEPLOY_INCLUDE_LOCATION%" /S /I /F /R /K /Y /B
if %errorlevel% neq 0 exit /B %errorlevel%

xcopy "%INSTALL_LOCATION%\lib\*.lib" "%DEPLOY_LIB_LOCATION%" /I /F /R /K /Y /B
if %errorlevel% neq 0 exit /B %errorlevel%

echo Done.

endlocal
