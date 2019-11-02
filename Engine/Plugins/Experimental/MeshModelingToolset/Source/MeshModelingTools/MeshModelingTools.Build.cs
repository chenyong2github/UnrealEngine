// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MeshModelingTools : ModuleRules
{
	public MeshModelingTools(ReadOnlyTargetRules Target) : base(Target)
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
				// These dependencies were commented out when we split out MeshModelingToolsEditorOnly
				// Many of them are editor only and should not be depended on here.  If you re-add any
				// dependent modules, please confirm by "launching" or packaging that you are not introducing
				// editor dependencies
				//
				//"MeshDescription",
				//"ProxyLODMeshReduction", // currently required to be public due to IVoxelBasedCSG API

				"Core",
				"Eigen",
                "InteractiveToolsFramework",
				"GeometricObjects",
				"GeometryAlgorithms",
				"DynamicMesh",
				"MeshConversion",
                "MeshSolverUtilities",
				"ModelingComponents",
				"ModelingOperators",

				// ... add other public dependencies that you statically link with here ...
			}
            );
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// These dependencies were commented out when we split out MeshModelingToolsEditorOnly
				// Many of them are editor only and should not be depended on here.  If you re-add any
				// dependent modules, please confirm by "launching" or packaging that you are not introducing
				// editor dependencies
				//
				//"RenderCore",
				//"RHI",
				//"MeshUtilities",    // temp for saving mesh asset
				//"UnrealEd",
				//"MeshBuilder",
                //"MeshDescriptionOperations",
				//"MeshUtilitiesCommon",
				//"MeshReductionInterface", // for UE4 standard simplification
                //"ProxyLODMeshReduction", // for mesh merging voxel-based csg
				//"Slate",
				//"SlateCore",

				"CoreUObject",
				"Engine",
				"ModelingOperators",
				"InputCore",

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
