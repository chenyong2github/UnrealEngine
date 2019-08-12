// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class WindowsMixedRealityInterop : ModuleRules
{
    public WindowsMixedRealityInterop(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        string WMRIPath = Path.Combine(Target.UEThirdPartySourceDirectory, "WindowsMixedRealityInterop");
        string IncludePath = Path.Combine(WMRIPath, "Include");
        string LibrariesPath = Path.Combine(WMRIPath, "Lib");
        bool bAddLibraries = true;
        PublicIncludePaths.Add(IncludePath);
		PublicIncludePaths.Add(Path.Combine(IncludePath, "HoloLens1Remoting"));

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            LibrariesPath = Path.Combine(LibrariesPath, "x64");

            if (Target.Configuration == UnrealTargetConfiguration.Debug)
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "MixedRealityInteropDebug.lib"));
            }
            else
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "MixedRealityInterop.lib"));
            }
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "Microsoft.Holographic.AppRemoting.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "HolographicStreamerDesktop.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "Microsoft.Perception.Simulation.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "PerceptionSimulationManager.lib"));
        }
        else if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            LibrariesPath = Path.Combine(LibrariesPath, Target.WindowsPlatform.GetArchitectureSubpath());
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "MixedRealityInteropHoloLens.lib"));
        }
        else
        {
            bAddLibraries = false;
        }

        PublicLibraryPaths.Add(LibrariesPath);

        if (bAddLibraries)
        {
            // Win10 support
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "onecore.lib"));
            // Explicitly load lib path since name conflicts with an existing lib in the DX11 dependency.
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "d3d11.lib"));
        }

		// Add a dependency to SceneUnderstanding.dll if present
		string SceneUnderstandingPath = Path.Combine(Target.UEThirdPartyBinariesDirectory, "HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.MixedReality.SceneUnderstanding.dll");
		if (File.Exists(SceneUnderstandingPath))
		{
			PublicDelayLoadDLLs.Add("Microsoft.MixedReality.SceneUnderstanding.dll");
			RuntimeDependencies.Add(SceneUnderstandingPath);
			PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=0");
		}
	}
}

