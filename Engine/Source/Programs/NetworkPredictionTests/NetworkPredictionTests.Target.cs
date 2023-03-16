// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NetworkPredictionTestsTarget : TestTargetRules
{
	public NetworkPredictionTestsTarget(TargetInfo Target) : base(Target)
	{
		bCompileAgainstEngine = true;
		bUsesSlate = false;

		bUsePlatformFileStub = true;
		bMockEngineDefaults = true;

		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
	}
}
