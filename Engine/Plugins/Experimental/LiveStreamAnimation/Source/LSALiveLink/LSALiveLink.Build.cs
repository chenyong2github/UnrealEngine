// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LSALiveLink : ModuleRules
{
	public LSALiveLink(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"LiveStreamAnimation",
				"LiveLinkInterface",
				"LiveLink"
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"DeveloperSettings"
			}
		);
	}
}
