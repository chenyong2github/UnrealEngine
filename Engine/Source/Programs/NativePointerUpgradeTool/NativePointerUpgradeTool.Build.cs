// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NativePointerUpgradeTool : ModuleRules
{
	public NativePointerUpgradeTool(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add("Runtime/Launch/Public");

		PrivateIncludePaths.Add("Runtime/Launch/Private");		// For LaunchEngineLoop.cpp include

		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("Projects");
	}
}
