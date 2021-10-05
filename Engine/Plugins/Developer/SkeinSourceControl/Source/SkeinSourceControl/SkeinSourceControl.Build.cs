// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SkeinSourceControl : ModuleRules
{
	public SkeinSourceControl(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateDependencyModuleNames.AddRange(
            new string[] {
				"Core",
				"CoreUObject",
				"InputCore",
				"Slate",
				"SlateCore",
                "EditorStyle",
				"Engine",
				"SourceControl",
				"AssetRegistry",
				"UnrealEd"
			}
		);
	}
}
