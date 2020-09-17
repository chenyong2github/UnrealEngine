// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class HLMedia : ModuleRules
{
    public HLMedia(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        DynamicallyLoadedModuleNames.AddRange(
            new string[] {
                "Media",
            });

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "HLMediaFactory",
                "MediaAssets",
            });

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
			    "CoreUObject",
                "Engine",
                "Projects",
                "RenderCore",
                "RHI",
                "MediaUtils",
            });

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "Media",
            });

        PrivateIncludePaths.AddRange(
            new string[] {
                "HLMedia/Private",
            });

        var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

        if (Target.Platform == UnrealTargetPlatform.Win32
            ||
            Target.Platform == UnrealTargetPlatform.Win64
            ||
            Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            PrivateDependencyModuleNames.AddRange(new string[] 
            {
                "HLMediaLibrary",
                "D3D11RHI"
            });

            PrivateIncludePaths.AddRange(new string[]
            {
                Path.Combine(EngineDir, "Source/ThirdParty/HLMediaLibrary/inc"),
                Path.Combine(EngineDir, "Source/Runtime/Windows/D3D11RHI/Private"),
            });

            // required by D3D11RHI to compile
            AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
            AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
            AddEngineThirdPartyPrivateStaticDependencies(Target, "HLMediaLibrary");

            PublicSystemLibraries.Add("mfplat.lib");
            PublicSystemLibraries.Add("mfreadwrite.lib");
            PublicSystemLibraries.Add("mfuuid.lib");
        }

        if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/Windows/D3D11RHI/Private/Windows"));
        }
        else if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/Windows/D3D11RHI/Private/HoloLens"));
        }
    }
}
