// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ModelingOperatorsEditorOnly : ModuleRules
{
	public ModelingOperatorsEditorOnly(ReadOnlyTargetRules Target) : base(Target)
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
                "InteractiveToolsFramework",
                "MeshDescription",
				"StaticMeshDescription",
				"ModelingOperators",
				"GeometricObjects",
				"DynamicMesh",
				"MeshConversion",
				"MeshSolverUtilities",
				"GeometryAlgorithms", // required for constrained Delaunay triangulation
                "ProxyLODMeshReduction", // currently required to be public due to IVoxelBasedCSG API
				// ... add other public dependencies that you statically link with here ...
			}
            );
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",

				"MeshBuilder",
                "MeshDescriptionOperations",
				"MeshUtilitiesCommon",  
				"MeshReductionInterface", // for UE4 standard simplification 
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
