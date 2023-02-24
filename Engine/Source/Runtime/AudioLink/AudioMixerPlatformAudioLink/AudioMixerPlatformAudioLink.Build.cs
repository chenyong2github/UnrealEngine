// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioMixerPlatformAudioLink: ModuleRules
{
	public AudioMixerPlatformAudioLink(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateIncludePathModuleNames.Add("TargetPlatform");
		PublicIncludePathModuleNames.Add("AudioMixer");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"AudioMixer",
				"AudioMixerCore",
				"AudioLinkEngine",
				"SignalProcessing",
		});
					
		if (Target.bCompileAgainstEngine) 
		{
			PrivateDependencyModuleNames.Add("BinkAudioDecoder");

			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"UEOgg",
				"Vorbis",
				"VorbisFile"
				);
		}
	}
}
