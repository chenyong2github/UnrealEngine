// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosVD : ModuleRules
{
	public ChaosVD(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ChaosVDRuntime",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"DesktopPlatform",
				"Projects",
				"InputCore",
				"EditorFramework",
				"UnrealEd",
				"ToolMenus",
				"Slate",
				"SlateCore", 
				"OutputLog", 
				"SceneOutliner", 
				"WorkspaceMenuStructure",
			}
			);
	}
}
