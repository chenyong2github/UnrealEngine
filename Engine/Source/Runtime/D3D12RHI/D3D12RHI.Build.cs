// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class D3D12RHI : ModuleRules
{
	public D3D12RHI(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PrivateIncludePaths.Add("Runtime/D3D12RHI/Private/HoloLens");
		}
		PrivateIncludePaths.Add("Runtime/D3D12RHI/Private");
		PrivateIncludePaths.Add("../Shaders/Shared");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"RHI",
				"RHICore",
				"RenderCore",
				}
			);

		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateIncludePathModuleNames.AddRange(new string[] { "TaskGraph" });
		}

		///////////////////////////////////////////////////////////////
        // Platform specific defines
        ///////////////////////////////////////////////////////////////

		if (!Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
        {
            PrecompileForTargets = PrecompileTargetsType.None;
        }

		AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelExtensionsFramework");

        if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) ||
            Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "AMD_AGS");

			if (Target.WindowsPlatform.bPixProfilingEnabled &&
				Target.Configuration != UnrealTargetConfiguration.Shipping &&
				Target.Configuration != UnrealTargetConfiguration.Test)
            {
				PublicDefinitions.Add("PROFILE");
				PublicDependencyModuleNames.Add("WinPixEventRuntime");
			}

			if (Target.Platform != UnrealTargetPlatform.HoloLens)
            {
				PrivateDependencyModuleNames.Add("GeForceNOWWrapper");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
            	AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
            }
            else
            {
				PrivateDefinitions.Add("D3D12RHI_USE_D3DDISASSEMBLE=0");
			}
        }
    }
}
