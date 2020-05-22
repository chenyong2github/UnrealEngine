// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LinuxAArch64TargetPlatform : ModuleRules
{
    public LinuxAArch64TargetPlatform(ReadOnlyTargetRules Target) : base(Target)
    {
        BinariesSubFolder = "LinuxAArch64";

        PrivateDependencyModuleNames.AddRange(
            new string[] {
				"Core",
				"TargetPlatform",
				"DesktopPlatform",
				"Projects"
			}
        );

        if (Target.bCompileAgainstEngine)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
				"Engine"
				}
            );

            PrivateIncludePathModuleNames.Add("TextureCompressor");
        }

        PrivateIncludePaths.AddRange(
            new string[] {
				"Developer/Linux/LinuxTargetPlatform/Private"
			}
        );
    }
}
