// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ModelingToolsEditorMode : ModuleRules
{
	public ModelingToolsEditorMode(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
                //"ContentBrowser"
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
                "Core",
                "CoreUObject",
                "ApplicationCore",
                "Slate",
                "SlateCore",
                "Engine",
                "InputCore",
				"EditorFramework",
				"UnrealEd",
                "ContentBrowser",
				"ContentBrowserData",
                "LevelEditor",
				"StatusBar",
				"EditorStyle",
                "Projects",
				"TypedElementRuntime",
				"MeshDescription",
				"StaticMeshDescription",
                "InteractiveToolsFramework",
				"EditorInteractiveToolsFramework",
				"GeometryFramework",
				"ModelingComponents",
				"ModelingComponentsEditorOnly",
				"MeshModelingTools",
				"MeshModelingToolsExp",
				"MeshModelingToolsEditorOnly",
				"MeshModelingToolsEditorOnlyExp",
				"MeshLODToolset",
				"ToolWidgets",
				"EditorWidgets",
				"ModelingEditorUI",
				"StylusInput",
				"DeveloperSettings"

				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				"Settings"
			}
			);

		PublicDefinitions.Add("WITH_PROXYLOD=" + (Target.Platform == UnrealTargetPlatform.Win64 ? '1' : '0'));

	}
}
