// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DMXRuntime : ModuleRules
{
	public DMXRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "CoreUObject",
                "Engine",
				"DMXProtocol",
				"DMXProtocolArtNet",
				"DMXProtocolSACN",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Json"
			}
		);
	}
}