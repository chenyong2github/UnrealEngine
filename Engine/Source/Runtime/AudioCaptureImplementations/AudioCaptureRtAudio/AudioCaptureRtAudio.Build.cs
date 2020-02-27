// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioCaptureRtAudio : ModuleRules
{
	public AudioCaptureRtAudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("AudioCaptureCore");

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			PublicDefinitions.Add("WITH_RTAUDIO=1");
			PublicDefinitions.Add("WITH_AUDIOCAPTURE=1");

			// Allow us to use direct sound
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DirectSound");
		}
		else if(Target.Platform == UnrealTargetPlatform.XboxOne)
        {
            PublicDefinitions.Add("WITH_RTAUDIO=1");
            PublicDefinitions.Add("WITH_AUDIOCAPTURE=1");
        }
		else if(Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicFrameworks.AddRange(new string[] { "CoreAudio", "AudioUnit", "AudioToolbox" });
            PublicDefinitions.Add("WITH_RTAUDIO=1");
            PublicDefinitions.Add("WITH_AUDIOCAPTURE=1");
        }
		else
		{
			PublicDefinitions.Add("WITH_RTAUDIO=0");
		}
	}
}
