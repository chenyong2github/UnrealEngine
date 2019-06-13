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
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "HolographicAppRemoting.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "PerceptionDevice.lib"));
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
    }
}

