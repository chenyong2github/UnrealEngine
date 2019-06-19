REM @echo off
mkdir ..\..\..\..\Intermediate\ShaderConductor
pushd ..\..\..\..\Intermediate\ShaderConductor

	cmake -G "Visual Studio 15" -T host=x64 -A x64 ..\..\Engine\Source\ThirdParty\ShaderConductor\ShaderConductor

	REM p4 edit %THIRD_PARTY_CHANGELIST% ..\lib\...

	"%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Professional\MSBuild\15.0\Bin\MSbuild.exe" ALL_BUILD.vcxproj /nologo /v:m /p:Platform=x64;Configuration="RelWithDebInfo"

	copy Bin\RelWithDebInfo\dxcompiler.dll ..\..\Engine\Binaries\ThirdParty\ShaderConductor\Win64\dxcompiler_sc.dll
	copy Bin\RelWithDebInfo\ShaderConductor.dll ..\..\Engine\Binaries\ThirdParty\ShaderConductor\Win64
	copy Bin\RelWithDebInfo\ShaderConductor.pdb ..\..\Engine\Binaries\ThirdParty\ShaderConductor\Win64
	copy Lib\RelWithDebInfo\ShaderConductor.lib ..\..\Engine\Binaries\ThirdParty\ShaderConductor\Win64
	copy Bin\RelWithDebInfo\ShaderConductor.dll ..\..\Engine\Binaries\Win64
	copy Bin\RelWithDebInfo\dxcompiler.dll ..\..\Engine\Binaries\Win64\dxcompiler_sc.dll
	
popd
