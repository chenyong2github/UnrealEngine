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
                "UnrealEd",
                "ContentBrowser",
                "LevelEditor",
				"EditorStyle",
                "Projects",
                "InteractiveToolsFramework",
				"EditorInteractiveToolsFramework",
				"MeshModelingTools",
				"MeshModelingToolsEditorOnly",

                "ViewportInteraction",

				"StylusInput"

				// ... add private dependencies that you statically link with here ...	
			}
            );
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
