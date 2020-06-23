// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MeshPaintEditorMode : ModuleRules
	{
        public MeshPaintEditorMode(ReadOnlyTargetRules Target) : base(Target)
		{
   
            PublicDependencyModuleNames.AddRange(
                new string[] {
                    "Slate",
                    "SlateCore",
                    "EditorStyle",
                }
            );

            PrivateDependencyModuleNames.AddRange(
                new string[] {
				    "Core",
				    "CoreUObject",
				    "Engine",
                    "InputCore",
				    "UnrealEd",
					"InteractiveToolsFramework",
					"EditorInteractiveToolsFramework",
					"MeshPaintingToolset",
					"PropertyEditor",
					"MainFrame",
					"DesktopPlatform",
                    "RenderCore",
                    "RHI",
                }
            );

        }
    }
}