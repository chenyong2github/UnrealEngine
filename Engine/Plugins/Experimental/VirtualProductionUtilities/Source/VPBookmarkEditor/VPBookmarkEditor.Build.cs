// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VPBookmarkEditor : ModuleRules
{
	public VPBookmarkEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"VPBookmark",
				"Core",
				"CoreUObject",
				"Engine",
				"SlateCore",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"EditorFramework",
				"EditorStyle",
				"HeadMountedDisplay",
				"InputCore",
				"LevelEditor",
				"Slate",
				"ViewportInteraction",
				"VREditor",
				"UnrealEd",
			}
		);
	}
}
