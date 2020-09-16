// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AssetPlacementEdMode : ModuleRules
{
	public AssetPlacementEdMode(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
                "InputCore",
				"Engine",
				"Slate",
				"SlateCore",
                "EditorStyle",
				"EditorFramework",
				"UnrealEd",
			}
		);
	}
}
