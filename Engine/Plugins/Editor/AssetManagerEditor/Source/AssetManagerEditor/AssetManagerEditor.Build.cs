// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AssetManagerEditor : ModuleRules
{
	public AssetManagerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				"AssetManagerEditor/Private",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] { 
				"Core",
				"CoreUObject",
				"Engine",
				"TargetPlatform",
			}
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Slate",
				"SlateCore",
                "ApplicationCore",
                "InputCore",
				"EditorFramework",
				"UnrealEd",
				"AssetRegistry",
				"Json",
				"CollectionManager",
				"ContentBrowser",
				"ContentBrowserData",
				"WorkspaceMenuStructure",
				"EditorStyle",
				"AssetTools",
				"PropertyEditor",
				"GraphEditor",
				"BlueprintGraph",
				"KismetCompiler",
				"LevelEditor",
				"SandboxFile",
				"EditorWidgets",
				"TreeMap",
				"ToolMenus",
			}
		);
	}
}
