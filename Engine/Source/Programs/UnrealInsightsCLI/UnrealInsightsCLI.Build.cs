// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealInsightsCLI : ModuleRules
{
	public UnrealInsightsCLI(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add("Runtime/Launch/Public");
		PrivateIncludePaths.Add("Runtime/Launch/Private");
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("Projects");
		PrivateDependencyModuleNames.Add("TraceLog");
		PrivateDependencyModuleNames.Add("TraceServices");
	}
}
