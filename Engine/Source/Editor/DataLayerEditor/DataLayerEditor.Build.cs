// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DataLayerEditor : ModuleRules
{
	public DataLayerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add("Editor/DataLayerEditor/Public");

		PrivateIncludePaths.Add("Editor/DataLayerEditor/Private"); // For PCH includes (because they don't work with relative paths, yet)

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"EditorFramework",
				"EditorWidgets",
				"EditorStyle",
				"EditorSubsystem",
				"PropertyEditor",
				"Engine",
                "InputCore",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"SceneOutliner",
				"ToolMenus",
				"AssetTools",
			}
		);
	}
}
