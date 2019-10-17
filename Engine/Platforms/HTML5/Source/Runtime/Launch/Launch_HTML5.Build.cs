// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Launch_HTML5 : Launch
{
	public Launch_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] {
			"ALAudio",
			"AudioMixerSDL",
			"Analytics",
			"AnalyticsET"
		});

		AddEngineThirdPartyPrivateStaticDependencies(Target, "SDL2");
	}
}
