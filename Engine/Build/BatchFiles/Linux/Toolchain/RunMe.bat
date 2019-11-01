@REM @echo off



set TOOLCHAIN_VERSION=v15

set LLVM_VERSION=8.0.1



set SVN_BINARY=%CD%\..\..\..\..\..\..\Binaries\ThirdParty\svn\Win64\svn.exe

set CMAKE_BINARY=%CD%\..\..\..\..\..\..\Extras\ThirdPartyNotUE\CMake\bin\cmake.exe

set PYTHON_BINARY=%CD%\..\..\..\..\..\..\Binaries\ThirdParty\Python\Win64\python.exe

set NSIS_BINARY=C:\Program Files (x86)\NSIS\Bin\makensis.exe



for %%i in (python.exe) do set PYTHON_BINARY="%%~$PATH:i"

for %%i in (cmake.exe) do set CMAKE_BINARY="%%~$PATH:i"

for %%i in (svn.exe) do set SVN_BINARY="%%~$PATH:i"



set FILENAME=%TOOLCHAIN_VERSION%_clang-%LLVM_VERSION%-centos7



echo Building %FILENAME%.exe...



echo.

echo Using SVN: %SVN_BINARY%

echo Using CMake: %CMAKE_BINARY%

echo Using Python: %PYTHON_BINARY%



@REM We need to build in a directory with shorter path, so we avoid hitting path max limit.

set ROOT_DIR=%CD%



rm -rf %TEMP:\=/%\clang-build-%LLVM_VERSION%

mkdir %TEMP%\clang-build-%LLVM_VERSION%

pushd %TEMP%\clang-build-%LLVM_VERSION%



unzip -o %ROOT_DIR:\=/%/%FILENAME%-windows.zip -d OUTPUT



set RELEASE=%LLVM_VERSION:.=%

%SVN_BINARY% co http://llvm.org/svn/llvm-project/llvm/tags/RELEASE_%RELEASE%/final source



pushd source\tools

%SVN_BINARY% co http://llvm.org/svn/llvm-project/cfe/tags/RELEASE_%RELEASE%/final clang

%SVN_BINARY% co http://llvm.org/svn/llvm-project/lld/tags/RELEASE_%RELEASE%/final lld

popd



mkdir build

pushd build



%CMAKE_BINARY% -G "Visual Studio 14 Win64" -DLLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN=ON -DCMAKE_INSTALL_PREFIX="..\install" -DPYTHON_EXECUTABLE="%PYTHON_BINARY%" "..\source"

%CMAKE_BINARY% --build . --target install --config MinSizeRel



popd



for %%G in (aarch64-unknown-linux-gnueabi x86_64-unknown-linux-gnu) do (

    mkdir OUTPUT\%%G

    mkdir OUTPUT\%%G\bin

    mkdir OUTPUT\%%G\lib

    mkdir OUTPUT\%%G\lib\clang

    copy "install\bin\clang.exe" OUTPUT\%%G\bin

    copy "install\bin\clang++.exe" OUTPUT\%%G\bin

    copy "install\bin\ld.lld.exe" OUTPUT\%%G\bin

    copy "install\bin\lld.exe" OUTPUT\%%G\bin

    copy "install\bin\llvm-ar.exe" OUTPUT\%%G\bin

    copy "install\bin\llvm-profdata.exe" OUTPUT\%%G\bin

    copy "install\bin\LTO.dll" OUTPUT\%%G\bin

    xcopy "install\lib\clang" OUTPUT\%%G\lib\clang /s /e /y

)



@REM Create version file

echo %TOOLCHAIN_VERSION%_clang-%LLVM_VERSION%-centos7> OUTPUT\ToolchainVersion.txt



echo Packing final toolchain...



pushd OUTPUT

rm -rf %ROOT_DIR:\=/%/%FILENAME%.zip

zip %ROOT_DIR:\=/%/%FILENAME%.zip *

popd



if exist "%NSIS_BINARY%" (

    echo Creating %FILENAME%.exe...

    copy %ROOT_DIR%\InstallerScript.nsi .

    "%NSIS_BINARY%" /V4 InstallerScript.nsi

    move %FILENAME%.exe %ROOT_DIR%

) else (

    echo Skipping installer creation, because makensis.exe was not found.

    echo Install Nullsoft.

)



popd



echo.

echo Done.

