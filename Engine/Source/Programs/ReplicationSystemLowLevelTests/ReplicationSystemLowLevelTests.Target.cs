// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class ReplicationSystemLowLevelTestsTarget : TestTargetRules
{
	public ReplicationSystemLowLevelTestsTarget(TargetInfo Target) : base(Target)
	{
		bCompileAgainstEngine = true;
		bUsesSlate = false;

		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		// Network config
		bWithPushModel = true;
		bUseIris = true;

		GlobalDefinitions.Add("UE_TRACE_ENABLED=1");
	}
}
