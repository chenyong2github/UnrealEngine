// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class CameraCalibrationEditor : ModuleRules
	{
		public CameraCalibrationEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"ApplicationCore",
					"AppFramework",
					"AssetRegistry",
					"AssetTools",
					"CameraCalibration",
					"CameraCalibrationCore",
					"CinematicCamera",
					"Composure",
					"ComposureLayersEditor",
					"Core",
					"CoreUObject",
	                "CurveEditor",
					"EditorStyle",
					"EditorWidgets",
					"Engine",
					"InputCore",
					"Json",
					"JsonUtilities",
					"LiveLinkCamera",
					"LiveLinkComponents",
					"LiveLinkEditor",
					"LiveLinkInterface",
					"MediaAssets",
					"MediaFrameworkUtilities",
					"OpenCV",
					"OpenCVHelper",
					"PlacementMode",
					"PropertyEditor",
					"Settings",
					"Slate",
					"SlateCore",
					"ToolMenus",
					"UnrealEd",
					"WorkspaceMenuStructure",
				});
		}
	}
}
