// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AnalyticsMulticastEditor : ModuleRules
{
    public AnalyticsMulticastEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
                "Analytics",
                "AnalyticsVisualEditing",
				"Slate",
				"SlateCore",
				"Engine",
				"UnrealEd", // for Asset Editor Subsystem
				"PropertyEditor",
				"WorkspaceMenuStructure",
				"EditorStyle",
				"EditorWidgets",
				"Projects"
			}
			);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Settings",
				"IntroTutorials",
                "AssetTools"
			}
		);

        DynamicallyLoadedModuleNames.AddRange(
            new string[] {
                "AssetTools"
            }
        );

	}
}
