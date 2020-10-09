// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LevelSnapshotsEditor : ModuleRules
{
	public LevelSnapshotsEditor(ReadOnlyTargetRules Target) : base(Target)
	{		
		PublicDependencyModuleNames.AddRange(
			new string[]
				{
					"Core",
				}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
				{
					"AssetRegistry",
					"AssetTools",
					"CoreUObject",
					"ContentBrowser",
					"EditorStyle",
					"EditorSubsystem",
					"Engine",
					"InputCore",
					"LevelSnapshots",
					"PropertyEditor",
					"Slate",
					"SlateCore",
					"ToolMenus",
					"UnrealEd"
				}
			);
	}
}
