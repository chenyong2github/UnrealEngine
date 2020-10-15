// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SwitchboardEditor : ModuleRules
{
	public SwitchboardEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Blutility",
				"Core",
				"CoreUObject",
				"Engine",
				"EditorStyle",
				"InputCore",
				"MessageLog",
				"Projects",
				"Settings",
				"Slate",
				"SlateCore",
				"UnrealEd",
			});
	}
}
