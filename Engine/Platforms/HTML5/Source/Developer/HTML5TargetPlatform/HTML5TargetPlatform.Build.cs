// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

using System;
using System.IO;

public class HTML5TargetPlatform : ModuleRules
{
	public HTML5TargetPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
		BinariesSubFolder = "HTML5";

		PrivateDependencyModuleNames.AddRange(
			new string[] {
//				"Core",
//				"CoreUObject",
//				"Sockets",
//				"TargetPlatform",
//				"DesktopPlatform",
				"Core",
				"CoreUObject",
				"Slate",
				"SlateCore",
				"InputCore",
				"Sockets",
				"TargetPlatform",
				"DesktopPlatform",
				"RHI",
			}
		);

		// no need for all these modules if the program doesn't want developer tools at all (like UnrealFileServer)
		if (!Target.bBuildRequiresCookedData && Target.bBuildDeveloperTools)
		{

			if (Target.bCompileAgainstEngine)
			{
				PrivateDependencyModuleNames.Add("Engine");
//				PrivateIncludePathModuleNames.Add("TextureCompressor");
			}

			if (Target.Type == TargetType.Editor || Target.Type == TargetType.Program)
			{
				DynamicallyLoadedModuleNames.Add("HTML5TargetPlatform");
			}
		}


		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Settings",
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);
	}
}
