// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MacTargetPlatform : ModuleRules
{
	public MacTargetPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"TargetPlatform",
				"DesktopPlatform",
                "InputCore",
                "SlateCore",
                "Slate",
                "SourceControl"
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Settings"
			}
		);

		if (Target.bCompileAgainstEngine)
		{
			PrivateDependencyModuleNames.Add("Engine");
			PrivateIncludePathModuleNames.Add("TextureCompressor");
		}
	}
}
