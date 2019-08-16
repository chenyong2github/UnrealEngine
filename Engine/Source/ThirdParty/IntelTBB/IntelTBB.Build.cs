// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IntelTBB : ModuleRules
{
    public IntelTBB(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        if ((Target.Platform == UnrealTargetPlatform.Win64) ||
            (Target.Platform == UnrealTargetPlatform.Win32) ||
            (Target.Platform == UnrealTargetPlatform.HoloLens))
        {
            string IntelTBBPath = Target.UEThirdPartySourceDirectory + "IntelTBB/IntelTBB-4.4u3/";
            PublicSystemIncludePaths.Add(IntelTBBPath + "Include");

            string PlatformSubpath = Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x86 ? "Win32" : "Win64";
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
            {
                PublicLibraryPaths.Add(System.String.Format("{0}lib/{1}/vc14/{2}/", IntelTBBPath, PlatformSubpath, Target.WindowsPlatform.GetArchitectureSubpath()));
            }
            else
            {
                PublicLibraryPaths.Add(System.String.Format("{0}lib/{1}/vc14/", IntelTBBPath, PlatformSubpath));
            }

            // Disable the #pragma comment(lib, ...) used by default in MallocTBB...
            // We want to explicitly include the library.
            PublicDefinitions.Add("__TBBMALLOC_BUILD=1");

            string LibName = "tbbmalloc";
            if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
            {
                LibName += "_debug";
            }
            LibName += ".lib";
            PublicAdditionalLibraries.Add(LibName);
        }
		else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicSystemIncludePaths.AddRange(
                new string[] {
                    Target.UEThirdPartySourceDirectory + "IntelTBB/IntelTBB-4.0/include",
                }
            );

            PublicAdditionalLibraries.AddRange(
                new string[] {
                    Target.UEThirdPartySourceDirectory + "IntelTBB/IntelTBB-4.0/lib/Mac/libtbb.a",
                    Target.UEThirdPartySourceDirectory + "IntelTBB/IntelTBB-4.0/lib/Mac/libtbbmalloc.a",
                }
            );
        }
    }
}
