// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Text3D : ModuleRules
{
	public Text3D(ReadOnlyTargetRules Target) : base(Target)
	{
		bEnableExceptions = true;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"SlateCore",
			"RenderCore",
			"RHI",
			"MeshDescription",
			"StaticMeshDescription",
			"GeometricObjects",
			"GeometryAlgorithms",

			// 3rd party libraries
			"FreeType2",
			"HarfBuzz",
		});
	}
}
