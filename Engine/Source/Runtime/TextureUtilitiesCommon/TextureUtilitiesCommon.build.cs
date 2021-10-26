// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TextureUtilitiesCommon : ModuleRules
{
	public TextureUtilitiesCommon(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"DeveloperSettings",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
			});
	}
}