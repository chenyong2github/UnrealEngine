// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PoseSearchEditor : ModuleRules
{
	public PoseSearchEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"AnimGraph",
				"AnimationCore",
				"Core",
				"CoreUObject",
				"Engine",
				"PoseSearch",
				"ToolMenus",
				"EditorFramework",
				"UnrealEd",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"BlueprintGraph",
			}
		);
	}
}