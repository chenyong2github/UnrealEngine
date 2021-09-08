// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MergeActors : ModuleRules
{
	public MergeActors(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateIncludePaths.AddRange(
            new string[] {
				"Editor/MergeActors/Private",
				"Editor/MergeActors/Private/MeshMergingTool",
				"Editor/MergeActors/Private/MeshProxyTool"
			}
        );

        PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
                "ContentBrowser",
                "Documentation",
                "MeshUtilities",
                "PropertyEditor",
                "RawMesh",
                "WorkspaceMenuStructure",
                "MeshReductionInterface",
                "MeshMergeUtilities",
				"GeometryProcessingInterfaces"
			}
		);

		PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "MaterialUtilities",
                "Slate",
                "SlateCore",
                "EditorStyle",
                "EditorFramework",
                "UnrealEd",
				"ToolWidgets"
            }
        );

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
                "ContentBrowser",
                "Documentation",
                "MeshUtilities",
                "MeshMergeUtilities",
                "MeshReductionInterface",
				"GeometryProcessingInterfaces"
            }
		);
	}
}
