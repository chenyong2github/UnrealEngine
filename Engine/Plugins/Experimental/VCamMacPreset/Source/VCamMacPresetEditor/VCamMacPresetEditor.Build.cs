// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VCamMacPresetEditor : ModuleRules
{
	public VCamMacPresetEditor(ReadOnlyTargetRules Target) : base(Target)
	{
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
