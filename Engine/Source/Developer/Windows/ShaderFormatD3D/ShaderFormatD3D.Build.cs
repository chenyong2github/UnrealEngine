// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShaderFormatD3D : ModuleRules
{
	public ShaderFormatD3D(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("TargetPlatform");
		PrivateIncludePathModuleNames.Add("D3D11RHI");
		PrivateIncludePathModuleNames.Add("D3D12RHI");

        PrivateIncludePaths.Add("../Shaders/Shared");

        PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"RenderCore",
				"ShaderPreprocessor",
				"ShaderCompilerCommon",
				}
			);

		//DXC
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string DxDllsPath = "$(EngineDir)/Binaries/ThirdParty/Windows/DirectX/x64/";

            RuntimeDependencies.Add("$(TargetOutputDir)/dxil.dll", DxDllsPath + "dxil.dll");

			string BinaryFolder = Target.UEThirdPartyBinariesDirectory + "ShaderConductor/Win64";
			RuntimeDependencies.Add(BinaryFolder + "/dxcompiler.dll");
			RuntimeDependencies.Add(BinaryFolder + "/ShaderConductor.dll");

			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"ShaderConductor"
			);
		}
		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
	}
}
