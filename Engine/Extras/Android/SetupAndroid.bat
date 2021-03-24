@echo off
setlocal ENABLEEXTENSIONS
set KEY_NAME=HKLM\SOFTWARE\Android Studio
set VALUE_NAME=Path
set STUDIO_PATH=

FOR /F "tokens=2*" %%A IN ('REG.exe query "%KEY_NAME%" /v "%VALUE_NAME%"') DO (set STUDIO_PATH=%%B)

if "%STUDIO_PATH%" == "" (
	echo Android Studio not installed, please download Android Studio 3.5.3 from https://developer.android.com/studio
	pause
	exit /b 1
)
echo Android Studio Path: %STUDIO_PATH%

set VALUE_NAME=SdkPath
set STUDIO_SDK_PATH=
FOR /F "tokens=2*" %%A IN ('REG.exe query "%KEY_NAME%" /v "%VALUE_NAME%"') DO (set STUDIO_SDK_PATH=%%B)

set ANDROID_LOCAL=%LOCALAPPDATA%\Android\Sdk

if "%STUDIO_SDK_PATH%" == "" (
	IF EXIST "%ANDROID_LOCAL%" (
		set STUDIO_SDK_PATH=%ANDROID_LOCAL%
	) ELSE (
		IF EXIST "%ANDROID_HOME%" (
			set STUDIO_SDK_PATH=%ANDROID_HOME%
		) ELSE (
			echo Unable to locate local Android SDK location. Did you run Android Studio after installing?
			pause
			exit /b 1
		)
	)
)
echo Android Studio SDK Path: %STUDIO_SDK_PATH%

if DEFINED ANDROID_HOME (set a=1) ELSE (
	set ANDROID_HOME=%STUDIO_SDK_PATH%
	setx ANDROID_HOME "%STUDIO_SDK_PATH%"
)
if DEFINED JAVA_HOME (set a=1) ELSE (
	set JAVA_HOME=%STUDIO_PATH%\jre
	setx JAVA_HOME "%STUDIO_PATH%\jre"
)
set NDKINSTALLPATH=%STUDIO_SDK_PATH%\ndk\21.4.7075529
set PLATFORMTOOLS=%STUDIO_SDK_PATH%\platform-tools;%STUDIO_SDK_PATH%\tools

set KEY_NAME=HKCU\Environment
set VALUE_NAME=Path
set USERPATH=

FOR /F "tokens=2*" %%A IN ('REG.exe query "%KEY_NAME%" /v "%VALUE_NAME%"') DO (set USERPATH=%%B)

where.exe /Q adb.exe
IF /I "%ERRORLEVEL%" NEQ "0" (
	echo Current user path: %USERPATH%
	setx PATH "%USERPATH%;%PLATFORMTOOLS%"
	echo Added %PLATFORMTOOLS% to path
)

set SDKMANAGER=%STUDIO_SDK_PATH%\tools\bin\sdkmanager.bat
IF EXIST "%SDKMANAGER%" (
	echo Using sdkmanager: %SDKMANAGER%
) ELSE (
	set SDKMANAGER=%STUDIO_SDK_PATH%\cmdline-tools\latest\bin\sdkmanager.bat
	IF EXIST "%SDKMANAGER%" (
		echo Using sdkmanager: %SDKMANAGER%
	) ELSE (
		echo Unable to locate sdkmanager.bat. Did you run Android Studio and install cmdline-tools after installing?
		pause
		exit /b 1
	)
)

call "%SDKMANAGER%" "platform-tools" "platforms;android-28" "build-tools;28.0.3" "cmake;3.10.2.4988404" "ndk;21.4.7075529"

IF /I "%ERRORLEVEL%" NEQ "0" (
	echo Update failed. Please check the Android Studio install.
	pause
	exit /b 1
)

if EXIST "%NDKINSTALLPATH%" (
	echo Success!
	setx NDKROOT "%NDKINSTALLPATH%"
	setx NDK_ROOT "%NDKINSTALLPATH%"
) ELSE (
	echo Update failed. Did you accept the license agreement?
	pause
	exit /b 1
)

pause
exit /b 0
