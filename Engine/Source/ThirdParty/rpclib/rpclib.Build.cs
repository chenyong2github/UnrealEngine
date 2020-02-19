// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// @todo rcplib does support both Max and Linux, got to compile for those platforms first
[SupportedPlatforms("Win32", "Win64"/*, "Mac", "Linux"*/)]
public class RPCLib : ModuleRules
{
    public RPCLib(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Source", "include"));

        // The Win64 debug config doesn't link property atm. We don't reall need it,
        // just something to keep in mind
        string ConfigName = /*Target.Configuration == UnrealTargetConfiguration.Debug ? "Debug" : */"Release";
        string LibraryPath = "";
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            LibraryPath = Path.Combine(ModuleDirectory, "Lib", ConfigName, "Win64");
        }
        else if (Target.Platform == UnrealTargetPlatform.Win32)
        {
            LibraryPath = Path.Combine(ModuleDirectory, "Lib", ConfigName, "Win32");
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            // PublicAdditionalLibraries.Add(BaseLibPath + "lib/librpc.a");
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
        }

        if (LibraryPath.Length > 0)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "rpc.lib"));
        }
    }
}
