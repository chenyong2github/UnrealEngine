// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LevelSnapshots : ModuleRules
{
	public LevelSnapshots(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
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
				"Engine",
				"Kismet",
				"Slate",
				"SlateCore",
				"LevelSnapshotFilters"
			}
			);
	}
}
