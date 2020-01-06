// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
				"Analytics"
            }
        );

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetTools",
                "MovieSceneTools",
                "Settings",
			}
		);

        PrivateIncludePaths.AddRange(
            new string[] {
            }
        );
    }
}
