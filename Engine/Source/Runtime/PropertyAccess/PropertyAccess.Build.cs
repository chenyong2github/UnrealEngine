// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PropertyAccess : ModuleRules
{
	public PropertyAccess(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] 
		{ 
			"Core",
			"CoreUObject",
			"PropertyPath",
		});
	}
}
