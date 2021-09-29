// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

[SupportedPlatforms("Win64")]
public class HeadlessChaos : ModuleRules
{
	public HeadlessChaos(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add("Runtime/Launch/Public");

		// For LaunchEngineLoop.cpp include
		PrivateIncludePaths.Add("Runtime/Launch/Private");

		SetupModulePhysicsSupport(Target);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
                "ApplicationCore",
				"Core",
				"CoreUObject",
				"Projects",
                "GoogleTest",
				"GeometryCore",
				"ChaosVehiclesCore"
            }
        );


		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDefinitions.Add("GTEST_OS_WINDOWS=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicDefinitions.Add("GTEST_OS_MAC=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS || Target.Platform == UnrealTargetPlatform.TVOS)
		{
			PublicDefinitions.Add("GTEST_OS_IOS=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicDefinitions.Add("GTEST_OS_LINUX_ANDROID=1");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) || Target.Platform == UnrealTargetPlatform.PS4)
		{
			PublicDefinitions.Add("GTEST_OS_LINUX=1");
		}

		PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
		
		LowLevelTests.UpdateGeneratedPropertiesScriptFile(
			typeof(HeadlessChaos),
			"HeadlessChaos",
			"Headless Chaos",
			Target.LaunchModuleName,
			Path.Combine("Engine", "Binaries", Target.Platform.ToString(), "NotForLicensees"));
	}
}
