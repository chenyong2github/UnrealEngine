// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterLightCardEditor : ModuleRules
{
	public DisplayClusterLightCardEditor(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"DisplayCluster",
				"DisplayClusterConfiguration",
				"DisplayClusterOperator",
				"DisplayClusterLightCardEditorShaders",
				
				"Core",
				"CoreUObject",
				"Engine",
				"RenderCore",
				"Renderer",
				"RHI",
				"InputCore",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"EditorStyle",
				"WorkspaceMenuStructure",
				"AdvancedPreviewScene",
			});
	}
}
