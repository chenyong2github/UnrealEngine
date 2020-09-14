// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class StageMonitorCommon : ModuleRules
{
	public StageMonitorCommon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"StageDataCore"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"GameplayTags",
				"VPUtilities"
			}
		);
	}
}
