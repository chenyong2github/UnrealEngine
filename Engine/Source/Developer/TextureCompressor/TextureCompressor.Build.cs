// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TextureCompressor : ModuleRules
{
	public TextureCompressor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"ImageCore",
				"TextureBuildUtilities",
				"TextureFormat",
			}
			);

		//AddEngineThirdPartyPrivateStaticDependencies(Target, "nvTextureTools");

		// TODO: TextureCompressorModule.cpp: warning: Use of memory after it is freed (within a call to 'GetResult') [cplusplus.NewDelete]
		StaticAnalyzerDisabledCheckers.Add("cplusplus.NewDelete");
	}
}
