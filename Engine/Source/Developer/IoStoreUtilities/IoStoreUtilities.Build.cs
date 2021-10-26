// Copyright Epic Games, Inc. All Rights Reserved.

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
            "AssetRegistry",
			"Zen",
			"DerivedDataCache",
			"RenderCore",
			"libcurl",
        });

		PublicIncludePathModuleNames.AddRange(new string[] {
			"Zen",
		});

		PrivateDependencyModuleNames.Add("PakFile");
        PrivateDependencyModuleNames.Add("Json");
        PrivateDependencyModuleNames.Add("RSA");
	}
}
