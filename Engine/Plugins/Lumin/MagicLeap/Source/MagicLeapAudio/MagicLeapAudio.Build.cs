// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class MagicLeapAudio : ModuleRules
{
	public MagicLeapAudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("TargetPlatform");

		var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
		PublicIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/AudioMixer/Public"));
		PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/AudioMixer/Private"));

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"MLSDK",
				"AudioMixer",
				"AudioMixerCore",
				"HeadMountedDisplay",
				"MagicLeap"
			}
		);

		AddEngineThirdPartyPrivateStaticDependencies(Target,
			"UEOgg",
			"Vorbis",
			"VorbisFile"
		);
	}
}
