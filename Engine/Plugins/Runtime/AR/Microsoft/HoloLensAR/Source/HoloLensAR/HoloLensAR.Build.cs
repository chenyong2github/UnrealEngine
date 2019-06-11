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

		if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            PublicDelayLoadDLLs.Add("QRCodesTrackerPlugin.dll");
            RuntimeDependencies.Add(Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "QRCodesTrackerPlugin.dll"));
        }

		PublicDelayLoadDLLs.Add("QRCodesTrackerPlugin.dll");
		RuntimeDependencies.Add(Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "QRCodesTrackerPlugin.dll"));

		AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

		PublicDefinitions.Add("WITH_WINDOWS_MIXED_REALITY=1");

		// Add a dependency to SceneUnderstanding.dll if present
		string SceneUnderstandingPath = Path.Combine(Target.UEThirdPartyBinariesDirectory, "HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "SceneUnderstanding.dll");
		if (File.Exists(SceneUnderstandingPath))
		{
			PublicDelayLoadDLLs.Add("SceneUnderstanding.dll");
			RuntimeDependencies.Add(SceneUnderstandingPath);
			PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=0");
		}
	}
}
