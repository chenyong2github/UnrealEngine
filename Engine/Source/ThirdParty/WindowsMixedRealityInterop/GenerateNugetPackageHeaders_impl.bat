setlocal
set WINRT_VER=%1
set NUGET_PACKAGE=%2
set SDK_VER=%3
set WINMD_FILE=%4
set OUT_DIR=%5
packages\Microsoft.Windows.CppWinRT.%WINRT_VER%\bin\cppwinrt.exe -in packages\%NUGET_PACKAGE%\build\native\include\%SDK_VER%\%WINMD_FILE% -ref %SDK_VER% -out %OUT_DIR%