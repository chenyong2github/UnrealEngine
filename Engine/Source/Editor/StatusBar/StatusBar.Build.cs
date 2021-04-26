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
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"EditorStyle",
				"EditorFramework",
				"UnrealEd",
				"ToolMenus",
				"OutputLog",
				"SourceControlWindows",
				"EditorSubsystem",
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
