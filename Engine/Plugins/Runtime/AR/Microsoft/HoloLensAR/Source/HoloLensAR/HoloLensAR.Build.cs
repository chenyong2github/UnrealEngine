// Copyright Epic Games, Inc. All Rights Reserved.

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
			
		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

        if (Target.Platform == UnrealTargetPlatform.Win64 || (Target.Platform == UnrealTargetPlatform.HoloLens && Target.WindowsPlatform.Architecture == WindowsArchitecture.x64))
        {
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "QRCodesTrackerPlugin.dll"));
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "QRCodesTrackerPlugin.winmd"));
        }
        else if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens/ARM64", "QRCodesTrackerPlugin.dll"));
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens/ARM64", "QRCodesTrackerPlugin.winmd"));

            // Add a dependency to SceneUnderstanding.dll if present
            string SceneUnderstandingDllPath = System.IO.Path.Combine(Target.UEThirdPartyBinariesDirectory, "HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.MixedReality.SceneUnderstanding.dll");
            string SceneUnderstandingWinMDPath = System.IO.Path.Combine(Target.UEThirdPartyBinariesDirectory, "HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.MixedReality.SceneUnderstanding.winmd");
            if (System.IO.File.Exists(SceneUnderstandingDllPath) && System.IO.File.Exists(SceneUnderstandingWinMDPath))
            {
                RuntimeDependencies.Add(SceneUnderstandingDllPath);
                RuntimeDependencies.Add(SceneUnderstandingWinMDPath);
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
