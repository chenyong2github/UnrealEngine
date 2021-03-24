@echo off
setlocal ENABLEEXTENSIONS

IF EXIST "%NDKROOT%" (
	echo Replacing ld.lld.exe at %NDKROOT%\toolchains\llvm\prebuilt\windows-x86_64\bin
	copy /y ld.lld.exe "%NDKROOT%"\toolchains\llvm\prebuilt\windows-x86_64\bin
) else (
	echo Unable to locate local Android NDK location. Did you run SetupAndroid to install it?
	pause
	exit /b 1
)

pause
exit /b 0
