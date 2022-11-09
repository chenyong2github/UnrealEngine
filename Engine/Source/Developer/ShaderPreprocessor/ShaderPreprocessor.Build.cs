// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShaderPreprocessor : ModuleRules
{
	public ShaderPreprocessor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"RenderCore",
			}
			);

		AddEngineThirdPartyPrivateStaticDependencies(Target, "MCPP");

		PrivateDefinitions.Add("STB_DS_IMPLEMENTATION");
		PrivateDefinitions.Add("STB_ALLOC_IMPLEMENTATION");
	}
}
