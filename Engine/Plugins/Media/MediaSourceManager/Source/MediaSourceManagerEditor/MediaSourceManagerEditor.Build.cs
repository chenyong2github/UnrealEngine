// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MediaSourceManagerEditor : ModuleRules
{
	public MediaSourceManagerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"MediaAssets",
				"MediaSourceManager",
				"Slate",
				"SlateCore",
				"UnrealEd",
			}
			);
	}
}
