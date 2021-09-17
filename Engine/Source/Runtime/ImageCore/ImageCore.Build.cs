// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ImageCore : ModuleRules
{
	public ImageCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("ColorManagement");

		PublicDependencyModuleNames.Add("Core");
	}
}
