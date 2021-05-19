// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MeshConversion : ModuleRules
{	
	public MeshConversion(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Note: The module purposefully doesn't have a dependency on CoreUObject.
		// If possible, we would like avoid having UObjects in GeometryProcessing
		// modules to keep the door open for writing standalone command-line programs
		// (which won't have UObject garbage collection).
        PublicDependencyModuleNames.AddRange(
			new string[] {
                "Core",
				"Engine",
                "MeshDescription",
				"StaticMeshDescription",
				"SkeletalMeshDescription",
				"GeometricObjects",
				"DynamicMesh"
            }
		);
    }
}
