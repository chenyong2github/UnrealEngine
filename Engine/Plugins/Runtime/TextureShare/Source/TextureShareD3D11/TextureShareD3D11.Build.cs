// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TextureShareD3D11 : ModuleRules
{
	public TextureShareD3D11(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"Engine",
			"RHI",
			"RenderCore",
			"D3D11RHI",
		});

		///////////////////////////////////////////////////////////////
		// Platform specific defines
		///////////////////////////////////////////////////////////////
		if (!Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
		}
	}
}
