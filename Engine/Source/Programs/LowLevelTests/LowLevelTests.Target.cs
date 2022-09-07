// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class LowLevelTestsTarget : TestTargetRules
{
	public LowLevelTestsTarget(TargetInfo Target) : base(Target)
	{
		bWithLowLevelTestsOverride = true;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
	}
}
