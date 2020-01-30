// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MeshConversion : ModuleRules
{	
	public MeshConversion(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
			new string[] {
                "Core",
                "MeshDescription",
				"StaticMeshDescription",
				"GeometricObjects",
				"DynamicMesh"
            }
		);

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject"
			}
		);
    }
}
