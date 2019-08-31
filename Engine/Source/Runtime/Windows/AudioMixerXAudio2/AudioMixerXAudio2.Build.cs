// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioMixerXAudio2 : ModuleRules
{
	public AudioMixerXAudio2(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("TargetPlatform");
		PublicIncludePaths.Add("Runtime/AudioMixer/Public");
		PrivateIncludePaths.Add("Runtime/AudioMixer/Private");

		if (Target.bCompileAgainstEngine)
        {
			// Engine module is required for CompressedAudioInfo implementations.
            PrivateDependencyModuleNames.Add("Engine");

			AddEngineThirdPartyPrivateStaticDependencies(Target,
			"UEOgg",
			"Vorbis",
			"VorbisFile"
			);
	
            if (Target.Platform == UnrealTargetPlatform.XboxOne)
            {
                PrivateDependencyModuleNames.Add("XMA2");
            }
        }
        PrivateDependencyModuleNames.AddRange(
			new string[] {
					"Core",
					"AudioMixerCore"
                }
		);

		PrecompileForTargets = PrecompileTargetsType.None;

		AddEngineThirdPartyPrivateStaticDependencies(Target,
			"DX11Audio"
        );

		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			PrecompileForTargets = PrecompileTargetsType.Any;
		}
	}
}
