setlocal
set WINRT_VER=2.0.190605.7
set NUGET_PACKAGE=Microsoft.Holographic.Remoting.2.0.16
set SDK_VER=10.0.18362.0
set WINMD=Microsoft.Holographic.AppRemoting.winmd
set OUT_DIR=Include/%SDK_VER%
call GenerateNugetPackageHeaders_impl.bat %WINRT_VER% %NUGET_PACKAGE% %SDK_VER% %WINMD% %OUT_DIR%
set SDK_VER=10.0.17763.0
set OUT_DIR=Include/%SDK_VER%
call GenerateNugetPackageHeaders_impl.bat %WINRT_VER% %NUGET_PACKAGE% %SDK_VER% %WINMD% %OUT_DIR%