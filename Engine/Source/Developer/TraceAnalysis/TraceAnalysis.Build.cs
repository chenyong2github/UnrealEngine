// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TraceAnalysis : ModuleRules
{
	public TraceAnalysis(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("DirectoryWatcher");
		PrivateDependencyModuleNames.Add("Sockets");
		PrivateDependencyModuleNames.Add("TraceLog");
	}
}
