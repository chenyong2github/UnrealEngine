// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterProjection : ModuleRules
{
	public DisplayClusterProjection(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PrivateIncludePaths.AddRange(
			new string[]
			{
				"DisplayClusterProjection/Private",
				"../../../../../Engine/Source/Runtime/Windows/D3D11RHI/Private",
				"../../../../../Engine/Source/Runtime/Windows/D3D11RHI/Private/Windows",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"Core",
				"Composure",
				"CoreUObject",
				"D3D11RHI",
				"DisplayCluster",
				"Engine",
				"HeadMountedDisplay",
				"MPCDI",
				"Projects",
				"RenderCore",
				"RHI",
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelExtensionsFramework");

		AddThirdPartyDependencies(ROTargetRules);
	}


	public void AddThirdPartyDependencies(ReadOnlyTargetRules ROTargetRules)
	{
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/"));

		string PathInc = string.Empty;
		string PathDll = string.Empty;

		// EasyBlend
		PathInc = Path.Combine(ThirdPartyPath, "EasyBlend", "Include");
		PathDll = Path.Combine(ThirdPartyPath, "EasyBlend", "DLL");
		
		PublicIncludePaths.Add(PathInc);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			RuntimeDependencies.Add(Path.Combine(PathDll, "mplEasyBlendSDKDX1164.dll"));
		}
	}
}
