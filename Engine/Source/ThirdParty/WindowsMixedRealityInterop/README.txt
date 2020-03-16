This solution builds libs and/or dlls (depending on output configuration) to wrap Windows Mixed Reality cppwinrt apis available in Windows 10 SDKs and one header to provide version info for UE4.

To build new interop binaries:
1) Open for edit the target files.  p4EditBinaries.bat will use BinaryFileList.txt to do this with perforce.
2) Right click solution and "restore nuget packages" just in case they did not automatically download.
3) Run GenerateNugetPackageHeaders.bat from Engine\Source\ThirdParty\WindowsMixedRealityInterop\.  (Only need to do this the first time on a new machine)
4) Build MixedRealtityInterop x64 debug and release, MixedRealityInteropHoloLens x64 and arm64 debug and release and submit the results to perforce if you change the implementation.
The visual studio 'Build->Batch Build' feature is useful here (check all the boxes).
5) Submit the target files along with whatever changes made it necessary to build.
*Note: there appears to be a file load race condition or similar in Visual Studio 2019 related to winrt nuget packages.  If one or a few configurations fail to build with winrt related errors you may simply need to close visual studio and open it again to get a successful build (I think doing this once has always worked for me).  Once you get one full success that instance of visual studio 2019 will continue to build successfully.

To update the holographic remoting version, the cppwinrt, or the windows sdk version you will need to do the following:
1) Install the remoting nuget package via 'manage nuget packages...' in the project context menu for the MixedRealityInterop project.
2) Edit GenerateNugetPackageHeaders.bat as necessary to use your current versions.
3) Open for edit everything under Engine\Source\ThirdParty\WindowsMixedRealityInterop\Include\<The SDK version you are targeting, ie 10.0.18362.0>
4) Run GenerateNugetPackageHeaders.bat from Engine\Source\ThirdParty\WindowsMixedRealityInterop\.
5) Un-install the remoting nuget package via 'manage nuget packages...' in the project context menu.
6) Build the interop binaries.

To update the azure spatial anchors nuget package:
1) open the interop solution
2) for both projects right click the project and select 'manage nuget packages'
3) update to the newer package version.
4) Edit CopyASANugetBinaries.bat to reflect the new package version you want to use.
5) Run CopyASANugetBinaries.bat to copy the binaries
6) If you had to add/remove/rename any of the binary files:
6 a) Update MicrosoftAzureSpatialAnchorsForWMR.Build.cs to package those binaries into the build and MicrosoftAzureSpatialAnchorsForWMR.cpp to load them at runtime.
6 b) Add/remove files from perforce as necessary.