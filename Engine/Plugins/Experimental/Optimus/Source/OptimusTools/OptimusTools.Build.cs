// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class OptimusTools : ModuleRules
    {
        public OptimusTools(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateIncludePaths.AddRange(
                new string[] {
                    "OptimusTools/Private",
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
					"Slate",
					"SlateCore",
					"EditorStyle",
					"EditorFramework",
					"Engine",
					"ToolMenus",
					"UnrealEd",
					"InputCore",
					"ModelingComponentsEditorOnly",
					"MeshModelingTools",
					"MeshModelingToolsEditorOnly",
					"ModelingToolsEditorMode",
					"InteractiveToolsFramework",
					"EditorInteractiveToolsFramework",
					"StylusInput",
				}
			);

        }
    }
}
