@echo off
setlocal

set ROOT_LOCATION=%cd%

set SOURCE_LOCATION=%ROOT_LOCATION%\hdf5

set BUILD_LOCATION=%ROOT_LOCATION%\Intermediate\hdf5
set INSTALL_LOCATION=%BUILD_LOCATION%\Install

set DEPLOY_LOCATION=%ROOT_LOCATION%\AlembicDeploy
set DEPLOY_INCLUDE_LOCATION=%DEPLOY_LOCATION%\include
set DEPLOY_LIB_LOCATION=%DEPLOY_LOCATION%\VS2015\x64\lib
set DEPLOY_BIN_LOCATION=%ROOT_LOCATION%\Binaries\Win64

if exist %BUILD_LOCATION% (
    rmdir %BUILD_LOCATION% /S /Q)
if exist %DEPLOY_INCLUDE_LOCATION%\*.h (
    del %DEPLOY_INCLUDE_LOCATION%\H5*.h %DEPLOY_INCLUDE_LOCATION%\hdf5.h /Q)
if exist %DEPLOY_LIB_LOCATION%\*hdf5* (
    del %DEPLOY_LIB_LOCATION%\*hdf5* /Q)
if exist %DEPLOY_BIN_LOCATION%\*hdf5* (
    del %DEPLOY_BIN_LOCATION%\*hdf5* /Q)

mkdir %BUILD_LOCATION%
pushd %BUILD_LOCATION%

echo Configuring build for HDF5...
cmake -G "Visual Studio 16 2019" %SOURCE_LOCATION%^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_LOCATION%"^
    -DBUILD_SHARED_LIBS=ON^
    -DHDF5_BUILD_CPP_LIB=OFF^
    -DHDF5_ENABLE_THREADSAFE=ON^
    -DHDF5_BUILD_HL_LIB=OFF^
    -DHDF5_BUILD_EXAMPLES=OFF^
    -DHDF5_BUILD_TOOLS=OFF^
    -DBUILD_TESTING=OFF^
    -DHDF5_EXTERNAL_LIB_PREFIX=""^
    -DCMAKE_SHARED_LIBRARY_PREFIX=""
if %errorlevel% neq 0 exit /B %errorlevel%

echo Building HDF5 for Debug...
cmake --build . --config Debug -j8
if %errorlevel% neq 0 exit /B %errorlevel%

echo Installing HDF5 for Debug...
cmake --install . --config Debug
if %errorlevel% neq 0 exit /B %errorlevel%

echo Building HDF5 for Release...
cmake --build . --config Release -j8
if %errorlevel% neq 0 exit /B %errorlevel%

echo Installing HDF5 for Release...
cmake --install . --config Release
if %errorlevel% neq 0 exit /B %errorlevel%

popd

echo Deploying HDF5...
xcopy "%INSTALL_LOCATION%\include" "%DEPLOY_INCLUDE_LOCATION%" /S /I /F /R /K /Y /B
if %errorlevel% neq 0 exit /B %errorlevel%

xcopy "%INSTALL_LOCATION%\lib\*.lib" "%DEPLOY_LIB_LOCATION%" /I /F /R /K /Y /B
if %errorlevel% neq 0 exit /B %errorlevel%

xcopy "%INSTALL_LOCATION%\bin\*hdf5*.dll" "%DEPLOY_BIN_LOCATION%" /I /F /R /K /Y /B
if %errorlevel% neq 0 exit /B %errorlevel%

echo Done.

endlocal
