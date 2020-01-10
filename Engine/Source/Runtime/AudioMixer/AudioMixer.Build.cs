// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AudioMixer : ModuleRules
	{
		public AudioMixer(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePathModuleNames.Add("TargetPlatform");
            PublicIncludePathModuleNames.Add("TargetPlatform");

            PublicIncludePathModuleNames.Add("Engine");

            PrivateIncludePaths.AddRange(
				new string[]
				{
					"Runtime/AudioMixer/Private",
				}
			);

            PublicIncludePaths.Add("Runtime/AudioMixer/Private");


			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CoreUObject",
					"Engine",
                    "NonRealtimeAudioRenderer",
                    "AudioMixerCore",
                    "SignalProcessing"
                }
			);

			AddEngineThirdPartyPrivateStaticDependencies(Target,
					"UEOgg",
					"Vorbis",
					"VorbisFile",
					"libOpus",
					"UELibSampleRate"
					);

			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
			{
				string PlatformName = Target.Platform == UnrealTargetPlatform.Win32 ? "Win32" : "Win64";

                string LibSndFilePath = Target.UEThirdPartyBinariesDirectory + "libsndfile/";
                LibSndFilePath += PlatformName;


                PublicAdditionalLibraries.Add(LibSndFilePath + "/libsndfile-1.lib");
				PublicDelayLoadDLLs.Add("libsndfile-1.dll");
				PublicIncludePathModuleNames.Add("UELibSampleRate");

                RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/libsndfile/" + PlatformName + "/libsndfile-1.dll");
            }
		}
	}
}
