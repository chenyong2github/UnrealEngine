// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayCluster : ModuleRules
{
	public DisplayCluster(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
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
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"Sockets",
				"TextureShare",
				"TextureShareCore",
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

		// 3rd party dependencies
		AddThirdPartyDependencies(ROTargetRules);
	}

	public void AddThirdPartyDependencies(ReadOnlyTargetRules ROTargetRules)
	{
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/"));

		string PathLib = string.Empty;
		string PathInc = string.Empty;

		// VRPN
		PathLib = Path.Combine(ThirdPartyPath, "VRPN/Lib");
		PathInc = Path.Combine(ThirdPartyPath, "VRPN/Include");
		PublicAdditionalLibraries.Add(Path.Combine(PathLib, "vrpn.lib"));
		PublicAdditionalLibraries.Add(Path.Combine(PathLib, "quat.lib"));
		PublicIncludePaths.Add(PathInc);
	}
}
