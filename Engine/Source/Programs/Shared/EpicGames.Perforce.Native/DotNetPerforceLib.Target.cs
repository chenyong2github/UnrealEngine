// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DotNetPerforceLibTarget : TargetRules
{
	public DotNetPerforceLibTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		LaunchModuleName = "DotNetPerforceLib";

		bShouldCompileAsDLL = true;

		bBuildDeveloperTools = false;
		bUseMallocProfiler = false;
		bCompileICU = false;
		bBuildWithEditorOnlyData = false;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = false;
	}
}
