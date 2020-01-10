// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AudioCapture : ModuleRules
	{
		public AudioCapture(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"AudioMixer",
					"AudioCaptureCore"
				}
			);

            if (Target.Platform == UnrealTargetPlatform.Win32 ||
                Target.Platform == UnrealTargetPlatform.Win64 ||
				Target.Platform == UnrealTargetPlatform.Mac   ||
                Target.Platform == UnrealTargetPlatform.XboxOne)
            {
                PrivateDependencyModuleNames.Add("AudioCaptureRtAudio");
            }
            else if (Target.Platform == UnrealTargetPlatform.PS4)
            {
                PrivateDependencyModuleNames.Add("AudioCapturePS4Voice");
            }
            else if (Target.Platform == UnrealTargetPlatform.IOS)
            {
                PrivateDependencyModuleNames.Add("AudioCaptureAudioUnit");
            }
            else if (Target.Platform == UnrealTargetPlatform.Android && Target.Platform != UnrealTargetPlatform.Lumin)
            {
                PrivateDependencyModuleNames.Add("AudioCaptureAndroid");
            }
            else if (Target.Platform == UnrealTargetPlatform.Switch)
            {
                PrivateDependencyModuleNames.Add("AudioCaptureSwitch");
            }
        }
	}
}