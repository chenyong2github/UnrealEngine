// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VPUtilities : ModuleRules
{
	public VPUtilities(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"CinematicCamera",
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags",
				"TimeManagement",
				"UMG",
				"VPBookmark",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
			}
		);

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Blutility",
					"LevelEditor",
					"UnrealEd",
					"ViewportInteraction",
					"VPBookmarkEditor",
					"VREditor",
				}
			);
		}
	}
}
