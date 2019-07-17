//
// Copyright (C) Google Inc. 2017. All rights reserved.
//

namespace UnrealBuildTool.Rules
{
    public class ResonanceAudio : ModuleRules
    {
        public ResonanceAudio(ReadOnlyTargetRules Target) : base(Target)
        {
            string ResonanceAudioPath = ModuleDirectory + "/Private/ResonanceAudioLibrary";
            string ResonanceAudioLibraryPath = ModuleDirectory + "/Private/ResonanceAudioLibrary/resonance_audio";
            string PFFTPath = ModuleDirectory + "/Private/ResonanceAudioLibrary/third_party/pfft";

            PublicIncludePaths.AddRange(
                new string[] {
                }
            );

            PrivateIncludePaths.AddRange(
                new string[] {
                    "ResonanceAudio/Private",
                    ResonanceAudioPath,
                    ResonanceAudioLibraryPath,
                    PFFTPath,
                    "../../../../Source/Runtime/AudioMixer/Private"
                }
            );

            PublicDependencyModuleNames.AddRange(
                new string[] {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "AudioMixer",
                    "ProceduralMeshComponent",
                }
            );

            PrivateIncludePathModuleNames.AddRange(
                new string[] {
                    "TargetPlatform"
                }
            );

            PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "InputCore",
                    "Projects",
                    "AudioMixer",
                    "ProceduralMeshComponent"
					
                }
            );

            if (Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.Add("UnrealEd");
                PrivateDependencyModuleNames.Add("Landscape");
            }

            bEnableShadowVariableWarnings = false;

            AddEngineThirdPartyPrivateStaticDependencies(Target,
                    "UEOgg",
                    "Vorbis",
                    "VorbisFile",
                    "Eigen"
                    );

			// Check whether Procedural Mesh is supported:
			if(Target.Platform == UnrealTargetPlatform.HTML5)
            {
                PrivateDefinitions.Add("SUPPORTS_PROCEDURAL_MESH=0");
            }
            else
            {
                PrivateDependencyModuleNames.Add("ProceduralMeshComponent");
                PrivateDefinitions.Add("SUPPORTS_PROCEDURAL_MESH=1");
            }

			if(Target.Platform == UnrealTargetPlatform.Android)
            {
                PrivateDefinitions.Add("PFFFT_SIMD_DISABLE=1");
                PrivateDefinitions.Add("EIGEN_HAS_CXX11_MATH=0");
            }

            //Embree support:
            string SDKDir = Target.UEThirdPartySourceDirectory + "IntelEmbree/Embree2140/Win64/";
            PublicIncludePaths.Add(SDKDir + "include");

            if (!PublicDefinitions.Contains("USE_EMBREE=1"))
            {
                // In platforms that don't support Embree, we implement no-op versions of the functions, which means we need to statically link embree.
				PrivateDefinitions.Add("EMBREE_STATIC_LIB=1");
            }
        }
    }
}
