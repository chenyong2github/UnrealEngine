// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MeshModelingToolsEditorOnly : ModuleRules
{
	public MeshModelingToolsEditorOnly(ReadOnlyTargetRules Target) : base(Target)
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
				"GeometricObjects",
				"DynamicMesh",
				"MeshConversion",
				"MeshModelingTools",
				"ModelingComponents",
				"ModelingOperators",
				"ModelingOperatorsEditorOnly",
                "ProxyLODMeshReduction", // currently required to be public due to IVoxelBasedCSG API
				// ... add other public dependencies that you statically link with here ...

				"HairStrandsCore"		// required for Hair toolset
			}
            );
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
                "RenderCore",
                "RHI",
				"InputCore",

				"MeshUtilities",		// tangents calculation
				"UnrealEd",
				"MeshBuilder",
				"MeshUtilitiesCommon",  
				"MeshReductionInterface", // for UE4 standard simplification 
                "ProxyLODMeshReduction", // for mesh merging voxel-based csg
				//"Slate",
				//"SlateCore",
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
