// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioFormatOgg : ModuleRules
{
	public AudioFormatOgg(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("TargetPlatform");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Engine"
			}
			);

		if ((Target.Platform == UnrealTargetPlatform.Win64) ||
			(Target.Platform == UnrealTargetPlatform.Win32) ||
// @ATG_CHANGE : BEGIN HoloLens support
			(Target.Platform == UnrealTargetPlatform.HoloLens) ||
// @ATG_CHANGE : END
			(Target.Platform == UnrealTargetPlatform.Mac) ||
			(Target.Platform == UnrealTargetPlatform.Linux)
			//(Target.Platform == UnrealTargetPlatform.HTML5) // TODO test this for HTML5 !
		)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"UEOgg",
				"Vorbis",
				"VorbisFile"
				);
		}
	}
}
