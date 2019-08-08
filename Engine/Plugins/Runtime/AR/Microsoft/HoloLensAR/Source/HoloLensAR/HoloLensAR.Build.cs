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

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDelayLoadDLLs.Add("QRCodesTrackerPlugin.dll");
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "QRCodesTrackerPlugin.dll"));
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "QRCodesTrackerPlugin.winmd"));
        }
        if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            PublicDelayLoadDLLs.Add("QRCodesTrackerPlugin.dll");
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens/ARM64", "QRCodesTrackerPlugin.dll"));
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens/ARM64", "QRCodesTrackerPlugin.winmd"));
        }

        AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

		PublicDefinitions.Add("WITH_WINDOWS_MIXED_REALITY=1");

		// Add a dependency to SceneUnderstanding.dll if present
		string SceneUnderstandingDllPath = System.IO.Path.Combine(Target.UEThirdPartyBinariesDirectory, "HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "SceneUnderstanding.dll");
        string SceneUnderstandingWinMDPath = System.IO.Path.Combine(Target.UEThirdPartyBinariesDirectory, "HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "SceneUnderstanding.winmd");
        if (System.IO.File.Exists(SceneUnderstandingDllPath) && System.IO.File.Exists(SceneUnderstandingWinMDPath))
		{
			PublicDelayLoadDLLs.Add("SceneUnderstanding.dll");
			RuntimeDependencies.Add(SceneUnderstandingDllPath);
            RuntimeDependencies.Add(SceneUnderstandingWinMDPath);
            PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=1");
		}
        else
        {
            PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=0");
        }
	}
}
