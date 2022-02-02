// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ModelingToolsEditorMode : ModuleRules
{
	public ModelingToolsEditorMode(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[]
			{
				// ... add public include paths required here ...
			}
		);

		PrivateIncludePaths.AddRange(
			new string[]
			{
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
				"CoreUObject",
				"Slate",
				"SlateCore",
				"Engine",
				"InputCore",
				"EditorFramework",
				"UnrealEd",
				"ContentBrowserData",
				"StatusBar",
				"EditorStyle",
				"Projects",
				"TypedElementRuntime",
				"InteractiveToolsFramework",
				"EditorInteractiveToolsFramework",
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
				"DeveloperSettings",
				// ... add private dependencies that you statically link with here ...	
			}
		);

		PublicDefinitions.Add("WITH_PROXYLOD=" + (Target.Platform == UnrealTargetPlatform.Win64 ? '1' : '0'));
	}
}