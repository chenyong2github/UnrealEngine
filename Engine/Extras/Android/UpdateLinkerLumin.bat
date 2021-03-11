@echo off
setlocal ENABLEEXTENSIONS

IF EXIST "%MLSDK%" (
	echo Replacing ld.lld.exe at %MLSDK%\tools\toolchains\llvm-8\bin
	copy /y ld.lld.exe "%MLSDK%"\tools\toolchains\llvm-8\bin
) else (
	echo Unable to locate local Magic Leap SDK location.
	pause
	exit /b 1
)

pause
exit /b 0
