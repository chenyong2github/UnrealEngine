// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MediaSourceManager : ModuleRules
{
	public MediaSourceManager(ReadOnlyTargetRules Target) : base(Target)
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
			}
			);
	}
}
