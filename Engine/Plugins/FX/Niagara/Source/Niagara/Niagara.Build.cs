// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Niagara : ModuleRules
{
    public Niagara(ReadOnlyTargetRules Target) : base(Target)
    {
		PrivateIncludePaths.Add("../../../../Shaders/Shared");

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "NiagaraCore",
                "NiagaraShader",
                "Core",
                "Engine",
                "TimeManagement",
                "TraceLog",
                "Renderer",
                "JsonUtilities",
				"Landscape",
				"Json",
				"AudioPlatformConfiguration",
				"SignalProcessing",
				"ApplicationCore",
				"DeveloperSettings"
			}
        );


        PublicDependencyModuleNames.AddRange(
            new string[] {
                "NiagaraCore",
                "NiagaraShader",
                "MovieScene",
				"MovieSceneTracks",
				"CoreUObject",
                "VectorVM",
                "RHI",
                "NiagaraVertexFactories",
                "RenderCore",
                "IntelISPC",
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
