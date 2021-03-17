// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PackagesDialog : ModuleRules
{
	public PackagesDialog(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("AssetTools");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core", 
				"CoreUObject", 
				"Engine", 
                "InputCore",
				"Slate", 
				"SlateCore",
                "EditorStyle",
				"UnrealEd",
				"SourceControl",
				"AssetRegistry"
			}
		);

		DynamicallyLoadedModuleNames.Add("AssetTools");
	}
}
