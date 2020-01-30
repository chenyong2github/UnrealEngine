// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SlateFileDialogs : ModuleRules
{
    public SlateFileDialogs(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject",
                "InputCore",
                "Slate",
                "SlateCore",
                "DirectoryWatcher",
            }
        );
    
        PrivateIncludePaths.AddRange(
            new string[] {
                "Developer/SlateFileDialogs/Private",  
            }
        );

        PrivateIncludePathModuleNames.Add("TargetPlatform");
    }
}
