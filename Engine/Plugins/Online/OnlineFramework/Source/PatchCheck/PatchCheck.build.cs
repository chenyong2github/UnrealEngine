// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class PatchCheck : ModuleRules
{
	public PatchCheck(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Engine",
				"HTTP",
				"OnlineSubsystem",
				"OnlineSubsystemUtils"
			}
			);
	}
}
