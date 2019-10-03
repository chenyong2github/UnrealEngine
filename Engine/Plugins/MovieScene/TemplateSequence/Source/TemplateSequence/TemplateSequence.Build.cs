// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TemplateSequence : ModuleRules
{
	public TemplateSequence(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("TemplateSequence/Private");

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"MovieScene",
				"TimeManagement",
				"LevelSequence"
				}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"MovieSceneTracks"
				}
		);
	}
}
