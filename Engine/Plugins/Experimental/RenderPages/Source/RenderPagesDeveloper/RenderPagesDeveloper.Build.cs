// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RenderPagesDeveloper : ModuleRules
{
	public RenderPagesDeveloper(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"KismetCompiler",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"BlueprintGraph",
				"Engine",
				"GraphEditor",
				"RenderPages",
				"Slate",
				"SlateCore",
				"UnrealEd",
			}
		);
	}
}