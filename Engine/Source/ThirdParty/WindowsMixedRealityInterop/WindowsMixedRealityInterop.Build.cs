// Copyright Epic Games, Inc. All Rights Reserved.

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

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            LibrariesPath = Path.Combine(LibrariesPath, "x64");

			//HACK: use the release version of the interop because the debug build isn't compatible with UE right now.
            //if (Target.Configuration == UnrealTargetConfiguration.Debug)
            //{
            //    PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "MixedRealityInteropDebug.lib"));
            //}
            //else
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "MixedRealityInterop.lib"));
            }

            PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=1");
        }
        else if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            LibrariesPath = Path.Combine(LibrariesPath, Target.WindowsPlatform.GetArchitectureSubpath());
			//HACK: use the release version of the interop because the debug build isn't compatible with UE right now.
			//         if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)  // Debug Win CRT compatibility problems prevent using the debug interop on x64 (which is for the device emulator). See also WindowsMixedRealityLibrary.Build.cs
			//{
			//	PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "MixedRealityInteropHoloLensDebug.lib"));
			//}
			//else
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "MixedRealityInteropHoloLens.lib"));
			}

            // Add a dependency to SceneUnderstanding.dll if present
            string SceneUnderstandingPath = Path.Combine(Target.UEThirdPartyBinariesDirectory, "HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.MixedReality.SceneUnderstanding.dll");
            if (File.Exists(SceneUnderstandingPath))
            {
                RuntimeDependencies.Add(SceneUnderstandingPath);
                PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=1");
            }
            else
            {
                PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=0");
            }
        }
        else
        {
            bAddLibraries = false;
            PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=0");
        }

        if (bAddLibraries)
        {
            // Win10 support
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "onecore.lib"));
            // Explicitly load lib path since name conflicts with an existing lib in the DX11 dependency.
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "d3d11.lib"));
        }
	}
}

