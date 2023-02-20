// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DerivedDataTool : ModuleRules
{
	public DerivedDataTool(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("Projects");
		PrivateDependencyModuleNames.Add("DerivedDataCache");
	}
}
