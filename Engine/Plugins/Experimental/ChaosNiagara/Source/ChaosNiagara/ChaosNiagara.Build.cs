// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosNiagara : ModuleRules
{
	public ChaosNiagara(ReadOnlyTargetRules Target) : base(Target)
	{
        //PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateIncludePaths.Add("ChaosNiagara/Private");
        PublicIncludePaths.Add(ModuleDirectory + "/Public");

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Projects",
				"Slate",
				"SlateCore",
				"NiagaraCore",
				"Niagara",
				"NiagaraShader",
				"RenderCore",
				"VectorVM",
				"RHI",
				"ChaosSolverEngine",
                "Chaos",
                "ChaosSolvers",
                "GeometryCollectionEngine",
                "GeometryCollectionCore",
				"PhysicsCore",
				"FieldSystemCore",
				"FieldSystemEngine",
			}
        );
					
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
              	"NiagaraCore",
				"Niagara",
				"NiagaraShader",
				"CoreUObject",
				"VectorVM",
				"RHI",
				"NiagaraVertexFactories",
                "ChaosSolverEngine",
                "GeometryCollectionEngine",
                "GeometryCollectionCore",
				"FieldSystemCore",
				"FieldSystemEngine"
            }
        );

		PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
	}
}
