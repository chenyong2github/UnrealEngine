// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VPUtilitiesEditor : ModuleRules
{
	public VPUtilitiesEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
                "Blutility",
				"Core",
				"CoreUObject",
				"EditorSubsystem",
				"VPUtilities",                
				"VREditor",
            }
        );

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"EditorStyle",
				"Engine",
				"GameplayTags",
				"LevelEditor",
				"Settings",
				"Slate",
				"SlateCore",
				"TimeManagement",
				"UMG",
				"UMGEditor",
				"UnrealEd",
				"VPBookmark",
				"WorkspaceMenuStructure",
                "CinematicCamera",
				"OSC",
				"PlacementMode"
            }
		);
	}
}
