// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MeshLODToolset : ModuleRules
{
	public MeshLODToolset(ReadOnlyTargetRules Target) : base(Target)
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
				"Eigen",
                "InteractiveToolsFramework",
				"GeometricObjects",
				"GeometryAlgorithms",
				"DynamicMesh",
				"MeshConversion",
				"MeshDescription",
                "StaticMeshDescription",
				"ModelingComponents",
				"ModelingOperators",
				"MeshModelingTools",
				"GeometryFlowCore",
				"GeometryFlowMeshProcessing",
				"GeometryFlowMeshProcessingEditor"

				// ... add other public dependencies that you statically link with here ...
			}
            );
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{

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
				"PhysicsCore",
				"RenderCore",

				"UnrealEd",
				"EditorScriptingUtilities",
				"AssetTools"

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
