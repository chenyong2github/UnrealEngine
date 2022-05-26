// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterLightCardEditor : ModuleRules
{
	public DisplayClusterLightCardEditor(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {

				"AdvancedPreviewScene",
				"AppFramework",
				"Core",
				"CoreUObject",
				"DisplayCluster",
				"DisplayClusterConfiguration",
				"DisplayClusterOperator",
				"DisplayClusterLightCardEditorShaders",
				"EditorStyle",
				"Engine",
				"InputCore",
				"OpenCV",
				"OpenCVHelper",
				"PropertyEditor",
				"RenderCore",
				"Renderer",
				"RHI",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"WorkspaceMenuStructure",
				"ToolWidgets",
			});
	}
}
