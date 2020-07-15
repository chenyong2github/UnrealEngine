// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LiveStreamAnimationEditor : ModuleRules
{
	public LiveStreamAnimationEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"LiveStreamAnimation",
				"Core",
				"CoreUObject",
				"UnrealEd",
				"AssetTools",
				"LiveLinkInterface"
			}
		);
	}
}
