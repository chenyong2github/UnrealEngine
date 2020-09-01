// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class InputBlueprintNodes : ModuleRules
	{
        public InputBlueprintNodes(ReadOnlyTargetRules Target) : base(Target)
        {
			PrivateIncludePaths.AddRange(
				new string[] {
					"InputBlueprintNodes/Private",
				}
			);

            PrivateDependencyModuleNames.AddRange(
				new string[] {
					"BlueprintGraph",
                    "Core",
					"CoreUObject",
                    "Engine",
					"EnhancedInput",
					"GraphEditor",
                    "InputCore",
					"KismetCompiler",
					"PropertyEditor",
                    "Slate",
                    "SlateCore",
                    "UnrealEd",
                }
            );
        }
    }
}