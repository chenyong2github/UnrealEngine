// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DynamicMesh : ModuleRules
{	
	public DynamicMesh(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // For GPUSkinPublicDefs.h
        PublicIncludePaths.Add("Runtime/Engine/Public");
        
        PublicIncludePathModuleNames.AddRange(
			new string[] {
				"AnimationCore",			// For the BoneWeights.h include
			}
		);

        PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"GeometricObjects",
				"GeometryAlgorithms"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"CoreUObject",
				"Eigen"
			}
		);

	}
}
