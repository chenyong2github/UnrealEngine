// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterConfiguration : ModuleRules
{
	public DisplayClusterConfiguration(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PublicDefinitions.Add("WITH_OCIO=0");

		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"DisplayCluster",
				"DisplayClusterPostprocess",
				"DisplayClusterProjection",
			});

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"CinematicCamera",
				"ActorLayerUtilities",
				"OpenColorIO"
			});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDependencyModuleNames.AddRange(
			new string[] {
				"TextureShare"
			});
		}

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"Json",
				"JsonUtilities",
			});
	}
}
