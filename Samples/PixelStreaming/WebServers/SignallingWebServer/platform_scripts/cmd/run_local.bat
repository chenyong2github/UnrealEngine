:: Copyright Epic Games, Inc. All Rights Reserved.
@echo off
if not "%1"=="am_admin" (powershell start -verb runas '%0' am_admin & exit /b)

pushd "%~dp0"

title Cirrus

pushd ..\..

::Install required deps
call powershell -command "%~dp0\setup.ps1"

::Run node server
::If running with frontend web server and accessing outside of localhost pass in --publicIp=<ip_of_machine>
node cirrus %*

popd

popd

pause
