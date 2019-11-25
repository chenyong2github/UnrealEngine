echo off
pushd "%~dp0"

echo ----------------
echo Building Android
echo ----------------

pushd Android
call BuildForAndroid.bat
IF %ERRORLEVEL% NEQ 0 Exit /B 1
popd

echo ------------
echo Building PS4
echo ------------

pushd PS4
call BuildForPS4.bat
IF %ERRORLEVEL% NEQ 0 Exit /B 1
popd

echo ---------------
echo Building Switch
echo ---------------

pushd Switch
call BuildForSwitch.bat
IF %ERRORLEVEL% NEQ 0 Exit /B 1
popd

echo ----------------
echo Building Windows
echo ----------------

pushd Windows
call BuildForWindows.bat
IF %ERRORLEVEL% NEQ 0 Exit /B 1
popd

echo ----------------
echo Building XboxOne
echo ----------------

pushd XboxOne
call BuildForXboxOne.bat
IF %ERRORLEVEL% NEQ 0 Exit /B 1
popd

popd