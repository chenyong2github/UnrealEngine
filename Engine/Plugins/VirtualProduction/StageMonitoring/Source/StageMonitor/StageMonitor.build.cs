// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class StageMonitor : ModuleRules
{
	public StageMonitor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"GameplayTags",
				"StageDataCore",
				"VPUtilities"
			}
		);
	}
}
