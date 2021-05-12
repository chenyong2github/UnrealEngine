@echo off

rem   Check if nodejs is in the env variable PATH 
for %%X in (node.exe) do (set node=%%~$PATH:X)
if not defined node (
  echo Did not find nodejs in PATH, looking in default installation folder
  if exist "%ProgramFiles%\nodejs\node.exe" (set node="%ProgramFiles%\nodejs\node.exe")
)

if not defined node (
  echo ERROR: Couldn't find node.js installed, Please install latest nodejs from https://nodejs.org/en/download/
  exit 1
)


rem Let's check if it is a modern nodejs
%node% -e "process.exit( process.versions.node.split('.')[0] );"
echo Found Node.js version %errorlevel% (%node%)

if %errorlevel% LSS 8 (
  echo ERROR: installed node.js version is too old, please install latest nodejs from https://nodejs.org/en/download/
  exit 1
)

rem redirecting all command line arguments to node script
"%node%" Scripts/start.js %*