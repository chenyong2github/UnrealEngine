// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MeshPaintingToolset : ModuleRules
	{
        public MeshPaintingToolset(ReadOnlyTargetRules Target) : base(Target)
		{
            PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "InputCore",
                    "Slate",
                    "SlateCore",
					"RenderCore",
					"RHI",
					"Slate",
					"SlateCore",
					"MeshDescription",
					"StaticMeshDescription",
					"UnrealEd"
                }
                );

                PublicDependencyModuleNames.AddRange(
                new string[] {
					"InteractiveToolsFramework",
					"EditorInteractiveToolsFramework",
                    "GeometricObjects"
                }
            );
        }
    }
}