// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ALAudio_HTML5 : ALAudio
{
	public ALAudio_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		AddEngineThirdPartyPrivateStaticDependencies(Target,
			"OpenAL",
			"UEOgg",
			"Vorbis",
			"VorbisFile"
		);

		// base set this to None
		PrecompileForTargets = PrecompileTargetsType.Default;
	}
}
