@echo off
setlocal EnableDelayedExpansion

for /f "tokens=*" %%a in (%~dp0/BinaryFileList.txt) do (
	rem set FILES=!FILES! --field "Files+=%PROJECT_DIR%\%%a"
	p4 edit %%a
)
rem echo !FILES!