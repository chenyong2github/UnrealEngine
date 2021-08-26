// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TextureUtilitiesCommon : ModuleRules
{
	public TextureUtilitiesCommon(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.Add("Core");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
			});
	}
}