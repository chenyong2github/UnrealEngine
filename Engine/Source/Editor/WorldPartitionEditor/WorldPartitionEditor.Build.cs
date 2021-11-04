// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WorldPartitionEditor : ModuleRules
{
    public WorldPartitionEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add("Editor/WorldPartitionEditor/Public");

        PrivateIncludePaths.Add("Editor/WorldPartitionEditor/Private");	// For PCH includes (because they don't work with relative paths, yet)

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
			}
		);
     
        PrivateDependencyModuleNames.AddRange(
            new string[] {
				"Core",
				"CoreUObject",
				"EditorFramework",
				"EditorStyle",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"WorldBrowser",
				"MainFrame",
				"PropertyEditor",
				"DeveloperSettings",
				"ToolMenus",
				"RenderCore",
				"Renderer",
				"RHI",
			}
		);

        DynamicallyLoadedModuleNames.AddRange(
            new string[] {
				"AssetTools",
            }
		);
    }
}
