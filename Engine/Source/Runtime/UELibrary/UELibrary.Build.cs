// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UELibrary : ModuleRules
{
	public UELibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "Engine", "InputCore" });

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "Launch", "ApplicationCore" });
		}
	}
}
