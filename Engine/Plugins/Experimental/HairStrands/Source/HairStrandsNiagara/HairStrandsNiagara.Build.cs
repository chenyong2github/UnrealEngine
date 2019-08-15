// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class HairStrandsNiagara : ModuleRules
	{
        public HairStrandsNiagara(ReadOnlyTargetRules Target) : base(Target)
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
                    "NiagaraCore",
					"Niagara",
					"NiagaraShader",
                    "RenderCore",
                    "Renderer",
                    "VectorVM",
					"RHI",
					"HairStrandsCore"
                }
			);
        }
	}
}
