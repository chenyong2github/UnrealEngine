// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterConfiguration : ModuleRules
{
	public DisplayClusterConfiguration(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"DisplayCluster",
				"DisplayClusterInput",
				"DisplayClusterPostprocess",
				"DisplayClusterProjection"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"Json",
				"JsonUtilities"
			});
	}
}
