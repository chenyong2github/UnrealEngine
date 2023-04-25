// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VCamPixelStreamingPresetEditor : ModuleRules
{
	public VCamPixelStreamingPresetEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		ShortName = "VCamPSPresetEditor";
		DefaultBuildSettings = BuildSettingsVersion.V2;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"CinematicCamera",
				"EditorWidgets",
				"Engine",
				"UnrealEd",
				"Settings",
				"PlacementMode",
				"VPUtilities",
				"VPUtilitiesEditor",
				"VCamCore",
				"VirtualCamera",
			}
		);
			
		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"LevelEditor",
			}
		);
	}
}
