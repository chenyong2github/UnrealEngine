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

set NUGET_PACKAGE=Microsoft.VCRTForwarders.140.1.0.5
p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\x64\concrt140_app.dll
p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\x64\msvcp140_1_app.dll
p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\x64\msvcp140_2_app.dll
p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\x64\msvcp140_app.dll
p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\x64\vcamp140_app.dll
p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\x64\vccorlib140_app.dll
p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\x64\vcomp140_app.dll
p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\x64\vcruntime140_1_app.dll
p4 edit ..\..\..\Binaries\ThirdParty\HoloLens\x64\vcruntime140_app.dll
copy packages\%NUGET_PACKAGE%\runtimes\win10-x64\native\release\concrt140_app.dll ..\..\..\Binaries\ThirdParty\Windows\x64\
copy packages\%NUGET_PACKAGE%\runtimes\win10-x64\native\release\msvcp140_1_app.dll ..\..\..\Binaries\ThirdParty\Windows\x64\
copy packages\%NUGET_PACKAGE%\runtimes\win10-x64\native\release\msvcp140_2_app.dll ..\..\..\Binaries\ThirdParty\Windows\x64\
copy packages\%NUGET_PACKAGE%\runtimes\win10-x64\native\release\msvcp140_app.dll ..\..\..\Binaries\ThirdParty\Windows\x64\
copy packages\%NUGET_PACKAGE%\runtimes\win10-x64\native\release\vcamp140_app.dll ..\..\..\Binaries\ThirdParty\Windows\x64\
copy packages\%NUGET_PACKAGE%\runtimes\win10-x64\native\release\vccorlib140_app.dll ..\..\..\Binaries\ThirdParty\Windows\x64\
copy packages\%NUGET_PACKAGE%\runtimes\win10-x64\native\release\vcomp140_app.dll ..\..\..\Binaries\ThirdParty\Windows\x64\
copy packages\%NUGET_PACKAGE%\runtimes\win10-x64\native\release\vcruntime140_1_app.dll ..\..\..\Binaries\ThirdParty\Windows\x64\
copy packages\%NUGET_PACKAGE%\runtimes\win10-x64\native\release\vcruntime140_app.dll ..\..\..\Binaries\ThirdParty\Windows\x64\