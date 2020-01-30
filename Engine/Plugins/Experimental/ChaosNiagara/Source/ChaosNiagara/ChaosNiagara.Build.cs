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
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"NiagaraCore",
				"Niagara",
				"NiagaraShader",
				"RenderCore",
                "ChaosSolverEngine",
                "Chaos",
                "ChaosSolvers",
                "GeometryCollectionEngine",
                "GeometryCollectionCore"
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
                "GeometryCollectionCore"
            }
        );
    }
}
