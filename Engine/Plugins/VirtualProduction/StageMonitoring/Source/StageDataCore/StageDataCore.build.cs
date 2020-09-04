// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class StageDataCore : ModuleRules
{
	public StageDataCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
                "Core",
				"CoreUObject",
				"Engine",
				"GameplayTags"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
            {
				"VPUtilities"
            }
		);
	}
}
