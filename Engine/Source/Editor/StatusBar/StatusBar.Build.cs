// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class StatusBar : ModuleRules
{
	public StatusBar(ReadOnlyTargetRules Target)
		 : base(Target)
	{
		PublicIncludePaths.Add(ModuleDirectory + "/Public");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"EditorFramework",
				"EditorStyle",
				"EditorSubsystem",
				"Engine",
				"InputCore",
				"OutputLog",
				"Slate",
				"SlateCore",
				"InputCore",
				"EditorFramework",
				"SourceControlWindows",
				"ToolMenus",
				"ToolWidgets",
				"UnrealEd",
			});

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"MainFrame",
			});

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"MainFrame",
			});
	}
}
