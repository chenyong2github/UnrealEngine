// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class HairStrandsCore : ModuleRules
	{
		public HairStrandsCore(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add(ModuleDirectory + "/Private");
			PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"Projects",
					"MeshDescription",
					"NiagaraCore",
					"Niagara",
					"NiagaraShader",
					"RenderCore",
					"Renderer",
					"VectorVM",
					"RHI",
					"Eigen",
					"StaticMeshDescription"
				});

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
					"DerivedDataCache",
					});
			}
		}
	}
}
