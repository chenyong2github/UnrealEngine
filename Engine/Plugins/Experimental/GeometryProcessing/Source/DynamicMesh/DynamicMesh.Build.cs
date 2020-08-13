// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DynamicMesh : ModuleRules
{	
	public DynamicMesh(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"GeometricObjects",
				"GeometryAlgorithms",

				"MeshUtilitiesCommon"		// currently required for FAllocator2D used in FDynamicMeshUVPacker
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Eigen"
			}
		);

	}
}
