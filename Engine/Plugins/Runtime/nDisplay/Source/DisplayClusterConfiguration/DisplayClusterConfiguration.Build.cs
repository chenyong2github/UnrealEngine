// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterConfiguration : ModuleRules
{
	public DisplayClusterConfiguration(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		DefaultBuildSettings = BuildSettingsVersion.V2;

		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"DisplayCluster",
				"DisplayClusterPostprocess",
				"DisplayClusterProjection",
				"OpenColorIO",
				"ActorLayerUtilities"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"Json",
				"JsonUtilities",
				"OpenColorIO",
				"ActorLayerUtilities"
			});

		PublicDefinitions.Add("WITH_OCIO=0");
	}
}
