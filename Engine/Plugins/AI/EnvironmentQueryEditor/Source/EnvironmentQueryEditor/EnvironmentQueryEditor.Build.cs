// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class EnvironmentQueryEditor : ModuleRules
{
	public EnvironmentQueryEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

		// These nodes are not public so are hard to subclass
		PrivateIncludePaths.AddRange(
			new string[] {
					"EnvironmentQueryEditor/Private",
					 Path.Combine(EngineDir, @"Source/Editor/GraphEditor/Private"),
					 Path.Combine(EngineDir, @"Source/Editor/AIGraph/Private")
			});

		PrivateIncludePathModuleNames.AddRange(
		   new string[] {
				"AssetRegistry",
				"AssetTools",
				"PropertyEditor",
				"DesktopPlatform",
				"LevelEditor",
		   });

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core", 
				"CoreUObject", 
                "InputCore",
				"Engine", 
                "RenderCore",
				"Slate",
				"SlateCore",
                "EditorStyle",
				"UnrealEd", 
				"MessageLog", 
				"GraphEditor",
				"KismetWidgets",
                "PropertyEditor",
				"AnimGraph",
				"BlueprintGraph",
                "AIGraph",
                "AIModule",
				"ToolMenus",
				"WorkspaceMenuStructure",
			}
		);
	}
}
