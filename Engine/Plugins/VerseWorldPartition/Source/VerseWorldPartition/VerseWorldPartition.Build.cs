// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VerseWorldPartition : ModuleRules
{
	public VerseWorldPartition(ReadOnlyTargetRules Target) : base(Target)
	{
		VersePath = "/UnrealEngine.com/WorldPartition";
		VerseScope = VerseScope.PublicAPI;

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(
			new string[]
			{
				"VerseWorldPartition/Private"
			}
		);
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"VerseAssets",
                "VerseNative",
			}
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine"
			}
		);
	}
}
