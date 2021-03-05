// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class USDMultiUser : ModuleRules
{
	public USDMultiUser(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ConcertSyncCore",
				"UnrealUSDWrapper",
				"UsdStage",
				"UsdUtilities",
			}
		);
	}
}
