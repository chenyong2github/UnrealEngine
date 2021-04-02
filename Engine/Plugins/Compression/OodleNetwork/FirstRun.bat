@echo off

echo This batch file enables Oodle as a packet handler by modifying config files.
echo.


REM This batch file can only work from within the Oodle folder.
REM assume you are in \Engine\Plugins\OodleNetwork
set BaseFolder="..\..\.."

if exist %BaseFolder:"=%\Engine goto SetUE4Editor

set /p BaseFolder=Type the base folder of UnrealEngine: 

if exist %BaseFolder:"=%\Engine goto SetUE4Editor

echo Could not locate Engine folder. 
goto End


:SetUE4Editor
set UE4EditorLoc="%BaseFolder:"=%\Engine\Binaries\Win64\UE4Editor.exe"

if exist %UE4EditorLoc:"=% goto GetGame

echo Could not locate UE4Editor.exe
goto End


:GetGame
set /p GameName=Type the name of the game you are working with: 
echo.



:EnableHandler
set HandlerCommandletParms=-run=OodleTrainerCommandlet enable
set FinalHandlerCmdLine=%GameName:"=% %HandlerCommandletParms% -forcelogflush


echo Executing Oodle PacketHandler enable commandlet - commandline:
echo %FinalHandlerCmdLine%

@echo on
%UE4EditorLoc:"=% %FinalHandlerCmdLine%
@echo off
echo.


if %errorlevel%==0 goto End

echo WARNING! Detected error when executing PacketHandler enable commandlet. Review the logfile.


:End
echo Execution complete.
pause


REM Put nothing past here.

