// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Water : ModuleRules
{
	public Water(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.Add("Runtime/Private");

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"Landmass",
				"RHI",
				"NavigationSystem",
				"AIModule",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"RenderCore",
				"NiagaraCore",
				"Niagara",
				"NiagaraShader",
				"Projects",
				"Landscape",
				"GeometricObjects",
				"DynamicMesh",
				"ChaosCore",
				"Chaos",
				"PhysicsCore",
				"DeveloperSettings"
			}
		);

		bool bWithWaterSelectionSupport = false;
		if (Target.bBuildEditor)
        {
			bWithWaterSelectionSupport = true;

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"SourceControl",
					"UnrealEd"
				}
			);
		}
		// Add a feature define instead of relying on the generic WITH_EDITOR define
		PublicDefinitions.Add("WITH_WATER_SELECTION_SUPPORT=" + (bWithWaterSelectionSupport ? 1 : 0));
	}
}