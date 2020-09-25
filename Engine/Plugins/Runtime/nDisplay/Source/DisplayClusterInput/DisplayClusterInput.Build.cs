// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterInput : ModuleRules
{
	public DisplayClusterInput(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"InputDevice",
				"HeadMountedDisplay",
				"DisplayCluster"
			});
		
		PrivateIncludePaths.AddRange(
			new string[] {
				"DisplayClusterInput/Private",
				"DisplayCluster/Private",
			});

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"InputDevice"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ApplicationCore",
				"DisplayCluster",
				"DisplayClusterConfiguration",
				"HeadMountedDisplay"
			});
	}
}
