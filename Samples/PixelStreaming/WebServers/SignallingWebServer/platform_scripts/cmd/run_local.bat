:: Copyright Epic Games, Inc. All Rights Reserved.
@echo off
if not "%1"=="am_admin" (powershell start -verb runas '%0' am_admin & exit /b)

pushd "%~dp0"

title Cirrus

pushd ..\..

call powershell -command "Set-ExecutionPolicy -Scope CurrentUser Unrestricted" || echo Failed to set script execution permissions

::Install required deps
call powershell -command "%~dp0\setup.ps1" || echo Failed to run setup PowerShell script you may need to run 'Set-ExecutionPolicy -Scope CurrentUser Unrestricted' in a PowerShell terminal && exit /b

::Run node server
::If running with frontend web server and accessing outside of localhost pass in --publicIp=<ip_of_machine>
node cirrus %*

popd

popd

pause
