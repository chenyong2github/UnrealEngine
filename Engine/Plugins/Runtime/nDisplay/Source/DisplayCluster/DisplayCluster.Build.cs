// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayCluster : ModuleRules
{
	public DisplayCluster(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PublicDefinitions.Add("WITH_OCIO=0");

		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"ActorLayerUtilities"
			});

		PrivateIncludePaths.AddRange(
			new string[] {
				"../../../../Source/Runtime/Renderer/Private",
			});

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"CinematicCamera",
				"Core",
				"CoreUObject",
				"DisplayClusterConfiguration",
				"Engine"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"D3D11RHI",
				"D3D12RHI",
				"HeadMountedDisplay",
				"InputCore",
				"Json",
				"JsonUtilities",
				"Networking",
				"Renderer",
				"RenderCore",
				"RHI",
				"RHICore",
				"Slate",
				"SlateCore",
				"Sockets",
				"TextureShare",
				"TextureShareCore",
				"OpenColorIO",
			});

		if (Target.bBuildEditor == true)
		{
			PublicIncludePathModuleNames.Add("DisplayClusterConfigurator");

			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("LevelEditor");
		}

		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelExtensionsFramework");
	}
}
