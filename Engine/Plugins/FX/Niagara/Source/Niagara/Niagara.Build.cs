// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Niagara : ModuleRules
{
    public Niagara(ReadOnlyTargetRules Target) : base(Target)
	{
		NumIncludedBytesPerUnityCPPOverride = 491520; // best unity size found from using UBT ProfileUnitySizes mode

		PrivateIncludePaths.Add("../../../../Shaders/Shared");

		// Enable OpenVDB under Windows
		// @todo: we need builds for OpenVDB for platforms other than windows
		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows) && Target.WindowsPlatform.Architecture != UnrealArch.Arm64)
		{
			// Specific to OpenVDB support
			bUseRTTI = true;
			bEnableExceptions = true;

			AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenVDB");
			PublicDefinitions.Add("UE_USE_OPENVDB=1");
		}
		
		PrivateDependencyModuleNames.AddRange(
            new string[] {
                "ApplicationCore",
                "AudioPlatformConfiguration",
                "Core",
                "DeveloperSettings",
                "Engine",
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

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows) && Target.WindowsPlatform.Architecture != UnrealArch.Arm64)
		{ 
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"IntelTBB",
				"Blosc",
				"zlib",
				"Boost"
			);
		}

		PublicDefinitions.AddRange(
            new string[]
            {
                "VECTORVM_SUPPORTS_EXPERIMENTAL=1",
                "VECTORVM_SUPPORTS_LEGACY=1"
            });
	}
}
