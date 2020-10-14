// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LevelSnapshotsEditor : ModuleRules
{
	public LevelSnapshotsEditor(ReadOnlyTargetRules Target) : base(Target)
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
				"Slate",
				"SlateCore",
				"UnrealEd",
				"LevelSnapshots",
				"LevelSnapshotFilters",
				"AssetRegistry",
				"AssetTools"
			}
			);
	}
}
