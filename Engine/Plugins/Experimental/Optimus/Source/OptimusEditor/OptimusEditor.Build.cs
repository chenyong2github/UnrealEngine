// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class OptimusEditor : ModuleRules
    {
        public OptimusEditor(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateIncludePaths.AddRange(
                new string[] {
                    "OptimusEditor/Private",
				}
            );

            PublicDependencyModuleNames.AddRange(
				new string[]
				{
				}
	        );

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
					"CoreUObject",
					"SlateCore",
					"Slate",
					"EditorStyle",
					"GraphEditor",
					"Engine",
					"EditorFramework",
					"UnrealEd",
					"AssetTools",
					"AdvancedPreviewScene",
					"InputCore",
					"RHI",
					"ToolMenus",
					"OptimusCore",
					"BlueprintGraph",		// For the graph pin colors
				}
			);

        }
    }
}
