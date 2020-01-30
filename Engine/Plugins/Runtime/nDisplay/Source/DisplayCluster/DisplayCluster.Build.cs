// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayCluster : ModuleRules
{
	public DisplayCluster(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(
			new string[] {
				"DisplayCluster/Private",
			});

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"D3D11RHI",
				"D3D12RHI",
				"Engine",
				"HeadMountedDisplay",
				"InputCore",
				"Json",
				"JsonUtilities",
				"Networking",
				"RHI",
				"RenderCore",
				"Slate",
				"SlateCore",
				"Sockets"
			});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
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
