// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LevelInstanceEditor : ModuleRules
{
    public LevelInstanceEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateIncludePaths.Add("Editor/LevelInstanceEditor/Private");	// For PCH includes (because they don't work with relative paths, yet)

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
            }
        );
     
        PrivateDependencyModuleNames.AddRange(
            new string[] {
				"Core",
				"CoreUObject",
				"EditorStyle",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"EditorFramework",
				"UnrealEd",
				"LevelEditor",
				"ToolMenus",
				"PropertyEditor",
				"NewLevelDialog",
				"MainFrame",
				"ContentBrowser",
				"AssetTools",
				"ClassViewer",
				"MessageLog",
				"EditorWidgets"
			}
		);

        DynamicallyLoadedModuleNames.AddRange(
            new string[] {
		    }
		);
    }
}
