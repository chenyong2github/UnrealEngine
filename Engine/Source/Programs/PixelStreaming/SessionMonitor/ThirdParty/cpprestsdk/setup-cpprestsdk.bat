@echo off

rem *********************************
rem Check if we have admin rights
rem *********************************
echo Administrative permissions required. Detecting permissions...
net session >nul 2>&1
if %errorLevel% == 0 (
	echo Success: Administrative permissions confirmed.
) else (
	echo Failure: Current permissions inadequate.
	exit /b 1
)

if exist .\vcpkg (
	echo Failure: Directory vcpkg already exists. Delete it and run this again for a fresh setup
	exit /B 1
)

git clone https://github.com/Microsoft/vcpkg.git .\vcpkg
if %errorlevel% NEQ 0 (
	echo Git clone failed.
	exit /B 1
)

cd vcpkg
call .\bootstrap-vcpkg.bat
if %errorlevel% NEQ 0 (
	echo Bootstrap failed
	exit /B 1
)

rem user-wide integration
.\vcpkg integrate install

rem install cpprestsdk
vcpkg install cpprestsdk cpprestsdk:x64-windows

