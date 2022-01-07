// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AnimationBlueprintLibrary : ModuleRules
{
	public AnimationBlueprintLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Kismet",
				"AnimGraph",
				"TimeManagement"
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"AnimGraph",
			}
		);
	}
}
