// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GeometricObjects : ModuleRules
{	
	public GeometricObjects(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",

				"MeshUtilitiesCommon"		// currently required for FAllocator2D used in FDynamicMeshUVPacker
			}
			);
	}
}
