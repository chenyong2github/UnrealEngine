@echo off
setlocal

call HDF5VS.bat
if %errorlevel% neq 0 exit /B %errorlevel%

call AlembicVS.bat
if %errorlevel% neq 0 exit /B %errorlevel%
