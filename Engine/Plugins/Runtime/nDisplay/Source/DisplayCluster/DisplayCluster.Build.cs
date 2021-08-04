// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayCluster : ModuleRules
{
	public DisplayCluster(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PublicDefinitions.Add("WITH_OCIO=0");

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

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDependencyModuleNames.AddRange(
			new string[] {
				"TextureShare"
			});
		}

		PrivateDependencyModuleNames.AddRange(
		new string[] {
			"HeadMountedDisplay",
			"InputCore",
			"Json",
			"JsonUtilities",
			"Networking",
			"OpenColorIO",
			"Renderer",
			"RenderCore",
			"RHI",
			"Slate",
			"SlateCore",
			"Sockets",
		});

		if (Target.bBuildEditor == true)
		{
			PublicIncludePathModuleNames.Add("DisplayClusterConfigurator");

			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("LevelEditor");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"D3D11RHI",
					"D3D12RHI",
					"TextureShare",
					"TextureShareCore",
			});

			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelExtensionsFramework");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
		}
	}
}
