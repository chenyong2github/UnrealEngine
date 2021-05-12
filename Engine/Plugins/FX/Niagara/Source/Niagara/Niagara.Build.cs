// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Niagara : ModuleRules
{
    public Niagara(ReadOnlyTargetRules Target) : base(Target)
    {
		PrivateIncludePaths.Add("../../../../Shaders/Shared");

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "ApplicationCore",
                "AudioPlatformConfiguration",
                "Core",
                "DeveloperSettings",
                "Engine",
                "HeadMountedDisplay",
                "Json",
                "JsonUtilities",
                "Landscape",
                "NiagaraCore",
                "NiagaraShader",
                "Renderer",
                "SignalProcessing",
                "TimeManagement",
                "TraceLog",
            }
        );


        PublicDependencyModuleNames.AddRange(
            new string[] {
                "CoreUObject",
                "IntelISPC",
                "MovieScene",
                "MovieSceneTracks",
                "NiagaraCore",
                "NiagaraShader",
                "NiagaraVertexFactories",
                "PhysicsCore",
                "RenderCore",
                "RHI",
                "VectorVM",
            }
        );

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"DerivedDataCache",
			});

        PrivateIncludePaths.AddRange(
            new string[] {
                "Niagara/Private",
            })
        ;

        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[] {
                "TargetPlatform",
				"EditorFramework",
                "UnrealEd",
				"SlateCore",
				"Slate"
            });
        }
    }
}
