// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BlankProgram : ModuleRules
{
	public BlankProgram(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("Projects");
	}
}
