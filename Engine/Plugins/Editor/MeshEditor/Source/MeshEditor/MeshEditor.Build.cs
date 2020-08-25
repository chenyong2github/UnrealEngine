// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MeshEditor : ModuleRules
	{
        public MeshEditor(ReadOnlyTargetRules Target) : base(Target)
		{
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
                    "SceneOutliner",
                    "RenderCore",
					"EditableMesh",
                    "MeshDescription",
					"StaticMeshDescription",
					"ViewportInteraction",
                    "VREditor",
					"Projects",
                    "RHI",
					"LevelEditor",
                    "MeshBuilder",
                    "BlastAuthoring",
					"Chaos",
                    "GeometryCollectionEngine",
                }
            );

            PrivateIncludePathModuleNames.AddRange(
				new string[] {
                    "ContentBrowser",
					"LevelEditor",
                    "MeshDescription",
                }
            );

            DynamicallyLoadedModuleNames.AddRange(
				new string[] {
                    "ContentBrowser"
                }
            );

            EnableMeshEditorSupport(Target);

        }
    }
}