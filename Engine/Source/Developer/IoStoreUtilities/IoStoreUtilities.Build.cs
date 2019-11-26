// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IoStoreUtilities : ModuleRules
{
	public IoStoreUtilities (ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateIncludePathModuleNames.AddRange(new string[] {
			"TargetPlatform",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Projects",
        });
        PrivateDependencyModuleNames.Add("PakFile");
        PrivateDependencyModuleNames.Add("Json");
        PrivateDependencyModuleNames.Add("RSA");
	}
}
