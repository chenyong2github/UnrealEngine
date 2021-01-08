// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MatineeToLevelSequence : ModuleRules
{
	public MatineeToLevelSequence(ReadOnlyTargetRules Target) : base(Target)
	{
		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
			"AssetTools",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
			"LevelSequence",
			"ContentBrowser",
			"Core",
			"CoreUObject",
			"EditorStyle",
			"Engine",
			"BlueprintGraph",
			"GameplayCameras",
			"InputCore",
			"Kismet",
			"LevelEditor",
			"MovieScene",
			"MovieSceneTools",
			"MovieSceneTracks",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UnrealEd",
			"TimeManagement",
			"TemplateSequence",
			"Analytics",
			"AssetRegistry"
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
			"AssetTools",
			"MovieSceneTools",
			"Settings",
			"WorkspaceMenuStructure",
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);
	}
}
