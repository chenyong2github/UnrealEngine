// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class HeadlessChaosTarget : TargetRules
{
	public HeadlessChaosTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;

        ExeBinariesSubFolder = "NotForLicensees";
		LaunchModuleName = "HeadlessChaos";

		bBuildDeveloperTools = false;

		// HeadlessChaos doesn't ever compile with the engine linked in
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = false;

        bHasExports = false;

        // UnrealHeaderTool is a console application, not a Windows app (sets entry point to main(), instead of WinMain())
        bIsBuildingConsoleApplication = true;

		GlobalDefinitions.Add("CHAOS_SERIALIZE_OUT=1");
	}
}
