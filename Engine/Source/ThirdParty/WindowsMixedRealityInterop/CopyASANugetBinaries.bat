setlocal
set NUGET_PACKAGE=Microsoft.Azure.SpatialAnchors.WinRT.2.1.1

p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\ARM64\Microsoft.Azure.SpatialAnchors.winmd
p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\ARM64\Microsoft.Azure.SpatialAnchors.dll
copy packages\%NUGET_PACKAGE%\lib\uap10.0\Microsoft.Azure.SpatialAnchors.winmd ..\..\..\Binaries\ThirdParty\HoloLens\ARM64\
copy packages\%NUGET_PACKAGE%\runtimes\win10-arm64\native\Microsoft.Azure.SpatialAnchors.dll ..\..\..\Binaries\ThirdParty\HoloLens\ARM64\

p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\x64\Microsoft.Azure.SpatialAnchors.winmd
p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\x64\Microsoft.Azure.SpatialAnchors.dll
copy packages\%NUGET_PACKAGE%\lib\uap10.0\Microsoft.Azure.SpatialAnchors.winmd ..\..\..\Binaries\ThirdParty\Windows\x64\
copy packages\%NUGET_PACKAGE%\runtimes\win10-x64\native\Microsoft.Azure.SpatialAnchors.dll ..\..\..\Binaries\ThirdParty\Windows\x64\
