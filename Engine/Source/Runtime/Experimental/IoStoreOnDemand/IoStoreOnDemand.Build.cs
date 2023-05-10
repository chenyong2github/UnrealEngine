// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IoStoreOnDemand : ModuleRules
{
	public IoStoreOnDemand(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicDependencyModuleNames.Add("Core");
		bAllowConfidentialPlatformDefines = true;
    }
}
