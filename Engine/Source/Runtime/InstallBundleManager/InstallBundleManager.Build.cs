// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class InstallBundleManager : ModuleRules
{
	public InstallBundleManager(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivatePCHHeaderFile = "Private/InstallBundleManagerPrivatePCH.h";

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Projects",
				"ApplicationCore",
				"Json"
			}
		);
	}
}
