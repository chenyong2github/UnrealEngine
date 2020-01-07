// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PolygonModeling : ModuleRules
	{
        public PolygonModeling(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
                new string[] {
				    "Core",
				    "CoreUObject",
					"Slate",
					"Engine",
					"InputCore",
					"UnrealEd",
                    "SceneOutliner",
                    "EditableMesh",
					"MeshEditor",
                    "MeshDescription",
					"StaticMeshDescription",
					"SlateCore",
					"ViewportInteraction",
                    "BlastAuthoring",
                    "GeometryCollectionCore",
                    "GeometryCollectionEngine",
                    "GeometryCollectionEditor"
                }
            );
		}
	}
}