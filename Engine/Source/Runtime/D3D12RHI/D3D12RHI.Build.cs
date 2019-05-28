// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class D3D12RHI : ModuleRules
{
	public D3D12RHI(ReadOnlyTargetRules Target) : base(Target)
	{
		// @ATG_CHANGE : BEGIN HoloLens support
		if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PrivateIncludePaths.Add("Runtime/D3D12RHI/Private/HoloLens");
		}
		// @ATG_CHANGE : END
		PrivateIncludePaths.Add("Runtime/D3D12RHI/Private");
		PrivateIncludePaths.Add("../Shaders/Shared");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Engine",
				"RHI",
				"RenderCore",
				"UtilityShaders",
				}
			);

		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateIncludePathModuleNames.AddRange(new string[] { "TaskGraph" });
		}

		///////////////////////////////////////////////////////////////
        // Platform specific defines
        ///////////////////////////////////////////////////////////////

        if (Target.Platform != UnrealTargetPlatform.Win32 && Target.Platform != UnrealTargetPlatform.Win64 && Target.Platform != UnrealTargetPlatform.XboxOne)
        {
            PrecompileForTargets = PrecompileTargetsType.None;
        }

        // @ATG_CHANGE : BEGIN HoloLens support
        if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32 ||
            Target.Platform == UnrealTargetPlatform.HoloLens)
        // @ATG_CHANGE : END
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
			// @ATG_CHANGE : BEGIN HoloLens support
			if (Target.Platform == UnrealTargetPlatform.HoloLens)
			{
				if (!Target.HoloLensPlatform.bBuildD3D12RHI)
				{
					//Log.TraceWarning("D3D12 RHI is being built, but HoloLens build settings indicate that it should not be.  Depending on your Windows SDK environment this may cause build errors.  Check build.cs files for dependencies.");
				}
			}
			else
			{
                AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "AMD_AGS");
            	AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
            	AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
            }
            // @ATG_CHANGE : END
		}
    }
}
