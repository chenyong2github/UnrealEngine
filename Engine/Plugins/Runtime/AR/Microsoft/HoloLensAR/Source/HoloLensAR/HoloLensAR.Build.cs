// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

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

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDelayLoadDLLs.Add("QRCodesTrackerPlugin.dll");
            RuntimeDependencies.Add(Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "QRCodesTrackerPlugin.dll"));
        }
        if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            PublicDelayLoadDLLs.Add("QRCodesTrackerPlugin.dll");
            RuntimeDependencies.Add(Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens/ARM64", "QRCodesTrackerPlugin.dll"));

			string SceneUnderstandingPath = Path.Combine(Target.UEThirdPartyBinariesDirectory, "HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "SceneUnderstanding.dll");
			if (File.Exists(SceneUnderstandingPath))
			{
				PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=0");
			}
        }

        AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

		PublicDefinitions.Add("WITH_WINDOWS_MIXED_REALITY=1");
	}
}
