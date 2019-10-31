//
// Copyright (C) Google Inc. 2017. All rights reserved.
//
using UnrealBuildTool;

public class ResonanceAudio : ModuleRules
{
	protected virtual bool bSupportsProceduralMesh { get { return true; } }

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

		ShadowVariableWarningLevel = WarningLevel.Off;

        AddEngineThirdPartyPrivateStaticDependencies(Target,
                "UEOgg",
                "Vorbis",
                "VorbisFile",
                "Eigen"
                );

		if (bSupportsProceduralMesh)
		{
			PrivateDependencyModuleNames.Add("ProceduralMeshComponent");
			PrivateDefinitions.Add("SUPPORTS_PROCEDURAL_MESH=1");
		}
		else
		{
			PrivateDefinitions.Add("SUPPORTS_PROCEDURAL_MESH=0");
		}

		if (Target.Platform == UnrealTargetPlatform.Android)
        {
            PrivateDefinitions.Add("PFFFT_SIMD_DISABLE=1");
            PrivateDefinitions.Add("EIGEN_HAS_CXX11_MATH=0");
        }

        //Embree support:
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string SDKDir = Target.UEThirdPartySourceDirectory + "IntelEmbree/Embree2140/Win64/";

            PublicIncludePaths.Add(SDKDir + "include");
            PublicAdditionalLibraries.Add(SDKDir + "lib/embree.2.14.0.lib");
            RuntimeDependencies.Add("$(TargetOutputDir)/embree.2.14.0.dll", SDKDir + "lib/embree.2.14.0.dll");
            RuntimeDependencies.Add("$(TargetOutputDir)/tbb.dll", SDKDir + "lib/tbb.dll");
            RuntimeDependencies.Add("$(TargetOutputDir)/tbbmalloc.dll", SDKDir + "lib/tbbmalloc.dll");
            PublicDefinitions.Add("USE_EMBREE=1");
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            string SDKDir = Target.UEThirdPartySourceDirectory + "IntelEmbree/Embree2140/MacOSX/";

            PublicIncludePaths.Add(SDKDir + "include");

            PrivateDefinitions.Add("USE_EMBREE=1");
        }
        else
        {
            // In platforms that don't support Embree, we implement no-op versions of the functions.
            string SDKDir = Target.UEThirdPartySourceDirectory + "IntelEmbree/Embree2140/";
            PublicIncludePaths.Add(SDKDir + "include");
            PublicDefinitions.Add("USE_EMBREE=0");
            PrivateDefinitions.Add("EMBREE_STATIC_LIB=1");
        }
    }
}
