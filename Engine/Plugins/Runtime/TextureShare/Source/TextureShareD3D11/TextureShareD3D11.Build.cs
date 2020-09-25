// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class TextureShareD3D11 : ModuleRules
{
	public TextureShareD3D11(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
						new string[]
						{
							"Core",
							"Engine",
							"RHI",
							"RenderCore",
							"D3D11RHI",
						});

		PrivateIncludePaths.AddRange(
			new string[]
			{
						"../../../../Source/Runtime/Windows/D3D11RHI/Private",
						"../../../../Source/Runtime/Windows/D3D11RHI/Private/Windows",
			});

		///////////////////////////////////////////////////////////////
		// Platform specific defines
		///////////////////////////////////////////////////////////////
		if (!Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && Target.Platform != UnrealTargetPlatform.XboxOne)
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) ||
			Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
			if (Target.Platform != UnrealTargetPlatform.HoloLens)
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelExtensionsFramework");
			}
		}
	}
}
