// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TimeManagementEditor : ModuleRules
{
	public TimeManagementEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Slate",
				"SlateCore",
			}
		);
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"EditorStyle",
				"Engine",
				"InputCore",
				"TimeManagement",
				"UnrealEd",
				"WorkspaceMenuStructure",
			}
		);
	}
}
