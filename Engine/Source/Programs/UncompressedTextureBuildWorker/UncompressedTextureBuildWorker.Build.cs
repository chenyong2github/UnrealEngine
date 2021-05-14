// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UncompressedTextureBuildWorker : ModuleRules
{
	public UncompressedTextureBuildWorker(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"DerivedDataCache",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"DerivedDataBuildWorker",
			"TextureBuild",
			"TextureFormat",
			"TextureFormatUncompressed",
		});
	}
}
