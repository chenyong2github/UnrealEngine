// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GeometryAlgorithms : ModuleRules
{	
	public GeometryAlgorithms(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"GeometricObjects"
			}
			);
	}
}
