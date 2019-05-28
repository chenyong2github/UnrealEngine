// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class D3D11RHI : ModuleRules
{
	public D3D11RHI(ReadOnlyTargetRules Target) : base(Target)
	{
// @ATG_CHANGE : BEGIN HoloLens support
		if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PrivateIncludePaths.Add("Runtime/Windows/D3D11RHI/Private/HoloLens");
		}
// @ATG_CHANGE : END
		PrivateIncludePaths.Add("Runtime/Windows/D3D11RHI/Private");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Engine",
				"RHI",
				"RenderCore",
				"UtilityShaders",
			}
			);

		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
// @ATG_CHANGE : BEGIN HoloLens support
		if (Target.Platform != UnrealTargetPlatform.HoloLens)
		{ 
        	AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "AMD_AGS");
        	AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
		}
// @ATG_CHANGE : END


        if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateIncludePathModuleNames.AddRange(new string[] { "TaskGraph" });
		}
	}
}
