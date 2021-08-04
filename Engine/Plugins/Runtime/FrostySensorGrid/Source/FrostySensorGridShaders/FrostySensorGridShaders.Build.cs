// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FrostySensorGridShaders : ModuleRules
{
    public FrostySensorGridShaders(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject",
                "Engine",
                "NiagaraCore",
                "Renderer",
            }
        );

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "RenderCore",
                "VectorVM",
                "RHI",
                "Projects",
                "NiagaraCore"
            }
        );

        if (Target.bBuildEditor == true)
        {
            PublicDependencyModuleNames.AddRange(
                new string[] {
                    "TargetPlatform",
                });

            PrivateDependencyModuleNames.AddRange(
                new string[] {
            });
        }

        PublicIncludePathModuleNames.AddRange(
            new string[] {
            });

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "DerivedDataCache",
                "Niagara",
            });
    }
}
