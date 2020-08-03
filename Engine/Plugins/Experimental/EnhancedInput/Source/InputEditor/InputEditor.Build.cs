// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class InputEditor : ModuleRules
	{
        public InputEditor(ReadOnlyTargetRules Target) : base(Target)
        {
			PrivateIncludePaths.AddRange(
				new string[] {
					"InputEditor/Private",
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"EnhancedInput",
                }
            );

            PrivateDependencyModuleNames.AddRange(
				new string[] {
					"BlueprintGraph",
                    "Core",
					"CoreUObject",
                    "DetailCustomizations",
                    "EditorStyle",
                    "Engine",
                    "GraphEditor",
					"InputBlueprintNodes",
					"InputCore",
					"KismetCompiler",
					"PropertyEditor",
                    "SharedSettingsWidgets",
                    "Slate",
                    "SlateCore",
                    "UnrealEd",
                }
            );
        }
    }
}