// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GLTFMaterialAnalyzer : ModuleRules
{
	public GLTFMaterialAnalyzer(ReadOnlyTargetRules Target) : base(Target)
	{
		//PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"RenderCore",
				"RHI",
			}
		);

		// NOTE: ugly hack to access HLSLMaterialTranslator to analyze materials
		PrivateIncludePaths.Add(EngineDirectory + "/Source/Runtime/Engine/Private");
	}
}
