// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HoloLensAR : ModuleRules
{
    public HoloLensAR(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "Engine",
				"MixedRealityInteropLibrary",
				"MRMesh"
				// ... add other public dependencies that you statically link with here ...
			}
			);

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
				"ApplicationCore",
                "HeadMountedDisplay",
                "AugmentedReality",
                "RenderCore",
                "RHI",
				"Projects",
				// ... add private dependencies that you statically link with here ...
			}
            );

		if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            PublicDelayLoadDLLs.Add("QRCodesTrackerPlugin.dll");
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "QRCodesTrackerPlugin.dll"));
        }

        AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

		PublicDefinitions.Add("WITH_WINDOWS_MIXED_REALITY=1");
	}
}
