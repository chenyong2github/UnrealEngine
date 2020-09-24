// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TextureFormatETC2 : ModuleRules
{
	public TextureFormatETC2(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"TargetPlatform",
				"TextureCompressor",
				"Engine"
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"ImageCore",
				"ImageWrapper"
			}
			);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "QualcommTextureConverter");
		}
		else
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "QualcommTextureConverter");
		}
	}
}
