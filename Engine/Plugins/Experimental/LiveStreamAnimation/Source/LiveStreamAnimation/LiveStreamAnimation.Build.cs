// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LiveStreamAnimation : ModuleRules
{
	public LiveStreamAnimation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "ForwardingChannels",
                "LiveLinkInterface",
				"LiveLink"
            }
        );

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine"
			}
		);
	}
}
