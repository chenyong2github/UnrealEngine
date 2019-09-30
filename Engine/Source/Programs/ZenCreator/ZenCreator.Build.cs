// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ZenCreator : ModuleRules
{
	public ZenCreator(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicIncludePaths.Add("Runtime/Launch/Public");

		PrivateIncludePaths.Add("Runtime/Launch/Private");		// For LaunchEngineLoop.cpp include

		PrivateDependencyModuleNames.Add("Core");
        PrivateDependencyModuleNames.Add("CoreUObject");
        PrivateDependencyModuleNames.Add("Projects");
        PrivateDependencyModuleNames.Add("PakFile");
        PrivateDependencyModuleNames.Add("Json");
        PrivateDependencyModuleNames.Add("RSA");
    }
}
