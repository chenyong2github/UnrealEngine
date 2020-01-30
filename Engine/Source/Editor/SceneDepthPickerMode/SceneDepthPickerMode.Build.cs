// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SceneDepthPickerMode : ModuleRules
{
    public SceneDepthPickerMode(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateIncludePaths.AddRange(
            new string[]
            {
                "Editor/UnrealEd/Private"
            }
        );

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
			}
		);
	}
}
