//
// Copyright (C) Google Inc. 2017. All rights reserved.
//

namespace UnrealBuildTool.Rules
{
    public class ResonanceAudio : ModuleRules
    {
        public ResonanceAudio(ReadOnlyTargetRules Target) : base(Target)
        {

            OptimizeCode = CodeOptimization.Never;

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
                    "ProceduralMeshComponent",
					"Eigen"
                }
            );

            // Resonance's Eigen usage is only valid with Eigen 2, so we set Eigen to backwards compatibility mode:
            // PublicDefinitions.Add("EIGEN2_SUPPORT=1");

            if (Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.Add("UnrealEd");
                PrivateDependencyModuleNames.Add("Landscape");
                PrivateDependencyModuleNames.Add("ProceduralMeshComponent");
            }

            AddEngineThirdPartyPrivateStaticDependencies(Target,
                    "UEOgg",
                    "Vorbis",
                    "VorbisFile"
                    );

            //Embree support:
            // EMBREE
            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                string SDKDir = Target.UEThirdPartySourceDirectory + "IntelEmbree/Embree2140/Win64/";

                PublicIncludePaths.Add(SDKDir + "include");
                PublicLibraryPaths.Add(SDKDir + "lib");
                PublicAdditionalLibraries.Add("embree.2.14.0.lib");
                RuntimeDependencies.Add("$(TargetOutputDir)/embree.2.14.0.dll", SDKDir + "lib/embree.2.14.0.dll");
                RuntimeDependencies.Add("$(TargetOutputDir)/tbb.dll", SDKDir + "lib/tbb.dll");
                RuntimeDependencies.Add("$(TargetOutputDir)/tbbmalloc.dll", SDKDir + "lib/tbbmalloc.dll");
                PublicDefinitions.Add("USE_EMBREE=1");
            }
            else if (Target.Platform == UnrealTargetPlatform.Mac)
            {
                string SDKDir = Target.UEThirdPartySourceDirectory + "IntelEmbree/Embree2140/MacOSX/";

                PublicIncludePaths.Add(SDKDir + "include");
                PublicAdditionalLibraries.Add(SDKDir + "lib/libembree.2.14.0.dylib");
                PublicAdditionalLibraries.Add(SDKDir + "lib/libtbb.dylib");
                PublicAdditionalLibraries.Add(SDKDir + "lib/libtbbmalloc.dylib");
                RuntimeDependencies.Add("$(TargetOutputDir)/libembree.2.14.0.dylib", SDKDir + "lib/libembree.2.14.0.dylib");
                RuntimeDependencies.Add("$(TargetOutputDir)/libtbb.dylib", SDKDir + "lib/libtbb.dylib");
                RuntimeDependencies.Add("$(TargetOutputDir)/libtbbmalloc.dylib", SDKDir + "lib/libtbbmalloc.dylib");
                PublicDefinitions.Add("USE_EMBREE=1");
            }
            else
            {
				// In platforms that don't support Embree, we implement no-op versions of the functions.
                string SDKDir = Target.UEThirdPartySourceDirectory + "IntelEmbree/Embree2140/Win64/";
                PublicIncludePaths.Add(SDKDir + "include");
                PublicDefinitions.Add("USE_EMBREE=0");
            }
        }
    }
}
