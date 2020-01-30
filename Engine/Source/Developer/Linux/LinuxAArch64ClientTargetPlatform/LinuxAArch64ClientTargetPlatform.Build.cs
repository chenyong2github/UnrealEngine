// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LinuxAArch64ClientTargetPlatform : ModuleRules
{
    public LinuxAArch64ClientTargetPlatform(ReadOnlyTargetRules Target) : base(Target)
    {
        BinariesSubFolder = "Linux";
		if (Target.Platform == UnrealTargetPlatform.LinuxAArch64)
		{
			BinariesSubFolder += "AArch64";
		}

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
