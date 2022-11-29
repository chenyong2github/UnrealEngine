// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FileUtilities : ModuleRules
{
	public FileUtilities(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] { "Core"});

		if (Target.bBuildEditor)
		{
			// Temporarily disabled while we fix libzip for mac
			if (Target.Platform != UnrealTargetPlatform.Mac)
			{
				PrivateDependencyModuleNames.AddRange(new string[] { "libzip" });
			}
		}
	}
}
