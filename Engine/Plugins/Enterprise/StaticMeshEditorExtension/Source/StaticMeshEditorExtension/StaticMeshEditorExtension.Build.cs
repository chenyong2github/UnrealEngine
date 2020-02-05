// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class StaticMeshEditorExtension : ModuleRules
	{
		public StaticMeshEditorExtension(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"EditableMesh",
					"EditorStyle",
					"Engine",
					"InputCore",
					"MainFrame",
					"MeshDescription",
					"MeshEditor",
					"MeshProcessingLibrary",
					"PolygonModeling",
					"Projects",
					"RawMesh",
					"RenderCore",
					"RHI",
					"RenderCore",
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"StaticMeshEditor",
					"UnrealEd",
					"ViewportInteraction",
					"DataprepCore",
                    "MeshUtilitiesCommon",
                }
			);

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"ContentBrowser",
					"MeshEditor",
					"MeshDescription",
					"StaticMeshEditor",
				}
			);

			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"ContentBrowser"
				}
			);
		}
	}
}