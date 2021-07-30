// Copyright Epic Games, Inc. All Rights Reserved.

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
				
		PublicIncludePathModuleNames.AddRange(
			new string[] {
				// "AnimationCore",			// For the BoneWeights.h include
			}
		);
	
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// "Eigen",
                "InteractiveToolsFramework",
				"GeometryCore",
				// "GeometryFramework",
				// "GeometryAlgorithms",
				"DynamicMesh",
				// "MeshConversion",
				// "MeshDescription",
				// "StaticMeshDescription",
				// "SkeletalMeshDescription",
				"ModelingComponents",
				// "ModelingOperators",

				// ... add other public dependencies that you statically link with here ...
			}
            );
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				// "RenderCore",
				"MeshModelingToolsExp",
				// "ModelingOperators",
				// "InputCore",
				// "PhysicsCore",

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
