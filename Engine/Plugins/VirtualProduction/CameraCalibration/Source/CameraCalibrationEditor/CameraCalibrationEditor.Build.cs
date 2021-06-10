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
					"Engine",
					"InputCore",
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
					"UnrealEd",
					"WorkspaceMenuStructure",
				});
		}
	}
}
