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
				"GeometryAlgorithms"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Eigen"
			}
		);

	}
}
