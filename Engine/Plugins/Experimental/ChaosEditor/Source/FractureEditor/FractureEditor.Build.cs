// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FractureEditor : ModuleRules
{
	public FractureEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
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
				"Core",
                "Voronoi",
				"InteractiveToolsFramework",
				"EditorInteractiveToolsFramework"

				// ... add other public dependencies that you statically link with here ...
			}
            );
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"EditorStyle",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"EditorFramework",
				"EditorScriptingUtilities",
				"ToolMenus",
				"UnrealEd",
				"LevelEditor",
                "GeometryCollectionEngine",
                "GeometryCollectionEditor",
				"ModelingComponents",
				"GeometryCore",
				"MeshDescription",
				"StaticMeshDescription",
				"PlanarCut",
				"Chaos",
				"ToolWidgets",
				"DeveloperSettings",

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
