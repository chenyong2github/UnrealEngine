// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class nDisplaySupportForLevelSnapshots : ModuleRules
{
	public nDisplaySupportForLevelSnapshots(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		ShortName = "nDisplaySnapshots";

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"LevelSnapshots",
				"DisplayCluster",
				"DisplayClusterConfiguration"
			}
			);
	}
}
