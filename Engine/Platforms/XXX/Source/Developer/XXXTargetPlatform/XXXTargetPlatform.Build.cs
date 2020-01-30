// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

using System;
using System.IO;

public class XXXTargetPlatform : ModuleRules
{
	public XXXTargetPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
		BinariesSubFolder = "XXX";

		PrivateDependencyModuleNames.AddRange(
			new string[] {
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

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Settings",
			}
		);

		// compile with Engine
		if (Target.bCompileAgainstEngine)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Engine",
					"Slate",
				}
			);

			PrivateIncludePathModuleNames.Add("TextureCompressor");
		}
	}
}
