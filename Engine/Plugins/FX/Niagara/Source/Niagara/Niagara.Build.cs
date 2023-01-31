// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Niagara : ModuleRules
{
    public Niagara(ReadOnlyTargetRules Target) : base(Target)
	{
		NumIncludedBytesPerUnityCPPOverride = 491520; // best unity size found from using UBT ProfileUnitySizes mode

		PrivateIncludePaths.Add("../../../../Shaders/Shared");

		// Specific to OpenVDB support
		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			bUseRTTI = true;
			bEnableExceptions = true;
		}
		
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
				System.IO.Path.Combine(GetModuleDirectory("Engine"), "Private"),
				System.IO.Path.Combine(GetModuleDirectory("Renderer"), "Private"),
			});

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

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows) && Target.Architecture.bIsX64)
		{ 
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"IntelTBB",
				"Blosc",
				"zlib",
				"Boost",				
				"OpenVDB"
			);

			PublicDefinitions.AddRange(
				new string[]
				{
					"NIAGARA_USE_OPENVDB=1"
				});
		}
		else
		{
			PublicDefinitions.AddRange(
				new string[]
				{
					"NIAGARA_USE_OPENVDB=0"
				});
		}

		PublicDefinitions.AddRange(
            new string[]
            {
                "VECTORVM_SUPPORTS_EXPERIMENTAL=1",
                "VECTORVM_SUPPORTS_LEGACY=1"
            });
	}
}
