This solution builds libs and/or dlls (depending on output configuration) to wrap Windows Mixed Reality cppwinrt apis available in Windows 10 SDKs and one header to provide version info for UE4.

Build MixedRealtityInterop x64 debug and release, MixedRealityInteropHoloLens x64 and arm64 debug and release and submit the results to perforce if you change the implementation.
The visual studio 'Build->Batch Build' feature is useful here.

The win32 configurations have not been used and are missing necessary binaries.
