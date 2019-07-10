// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PicpMPCDI : ModuleRules
{
	public PicpMPCDI(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				"PicpMPCDI/Private",
				"PicpProjection/Private",
				"MPCDI/Private",
				"../../../../../Engine/Source/Runtime/Windows/D3D11RHI/Private",
				"../../../../../Engine/Source/Runtime/Windows/D3D11RHI/Private/Windows",
				"../../../../../Engine/Source/Runtime/D3D12RHI/Private",
				"../../../../../Engine/Source/Runtime/D3D12RHI/Private/Windows"
			});

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Projects",
				"DisplayCluster",
				"PicpProjection",
				"MPCDI",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RenderCore",
				"DisplayCluster",
				"RHI",
				"UtilityShaders"
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		PublicAdditionalLibraries.Add("opengl32.lib");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenGL");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
	}
}
