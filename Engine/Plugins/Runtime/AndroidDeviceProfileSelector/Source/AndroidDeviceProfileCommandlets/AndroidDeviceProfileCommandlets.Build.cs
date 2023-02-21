// Copyright Epic Games, Inc. All Rights Reserved.
using EpicGames.Core;
using System.IO;
using UnrealBuildTool;

public class AndroidDeviceProfileCommandlets : ModuleRules
{
	public AndroidDeviceProfileCommandlets(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("AndroidDeviceDetection");

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Json",
				"JsonUtilities",
				"AndroidDeviceProfileSelector",
				"PIEPreviewDeviceSpecification"
			}
			);
	}
}
