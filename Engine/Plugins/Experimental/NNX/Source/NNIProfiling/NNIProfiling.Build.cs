// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NNIProfiling : ModuleRules
{
	public NNIProfiling(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange
			(
			new string[] {
				"Core",
				"Engine",
			}
		);

		PublicDefinitions.Add("WITH_NNI_STATS");
	}
}
