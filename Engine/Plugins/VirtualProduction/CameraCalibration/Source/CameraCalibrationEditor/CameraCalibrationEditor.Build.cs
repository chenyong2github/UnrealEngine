// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class CameraCalibrationEditor : ModuleRules
	{
		public CameraCalibrationEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			if (Target.Configuration == UnrealTargetConfiguration.Debug)
			{
				PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
				OptimizeCode = CodeOptimization.Never;
				bUseUnity = false;
				PCHUsage = PCHUsageMode.NoPCHs;
			}

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetRegistry",
					"AssetTools",
					"CameraCalibration",
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
					"PropertyEditor",
					"Slate",
					"SlateCore",
					"UnrealEd",
					"WorkspaceMenuStructure",
				});
		}
	}
}
