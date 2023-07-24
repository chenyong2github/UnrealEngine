// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class AndroidDeviceProfileCommandlets : ModuleRules
{
	public AndroidDeviceProfileCommandlets(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
			}
		);

		PublicIncludePathModuleNames.AddRange(
			new string[]
			{
				"AndroidDeviceDetection",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"Json",
				"JsonUtilities",
				"AndroidDeviceProfileSelector",
				"PIEPreviewDeviceSpecification"
			}
		);
	}
}
