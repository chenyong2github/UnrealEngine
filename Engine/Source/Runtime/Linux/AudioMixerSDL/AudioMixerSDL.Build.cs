// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioMixerSDL : ModuleRules
{
	public AudioMixerSDL(ReadOnlyTargetRules Target) : base(Target)
	{
        OptimizeCode = CodeOptimization.Never;

        PrivateIncludePathModuleNames.Add("TargetPlatform");
		PublicIncludePaths.Add("Runtime/AudioMixer/Public");
		PrivateIncludePaths.Add("Runtime/AudioMixer/Private");

		string PlatformName = Target.Platform.ToString();
		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			PlatformName = "Linux";
		}

		PrivateIncludePaths.Add("Runtime/Linux/AudioMixerSDL/Private/" + PlatformName);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
			}
			);

		PrivateDependencyModuleNames.Add("AudioMixer");

		AddEngineThirdPartyPrivateStaticDependencies(Target, 
			"UEOgg",
			"Vorbis",
			"VorbisFile",
			"SDL2"
			);
	}
}
