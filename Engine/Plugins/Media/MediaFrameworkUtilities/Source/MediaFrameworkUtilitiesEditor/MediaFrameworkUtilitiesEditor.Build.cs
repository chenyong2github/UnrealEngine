// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MediaFrameworkUtilitiesEditor : ModuleRules
	{
		public MediaFrameworkUtilitiesEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetRegistry",
					"AssetTools",
					"ClassViewer",
					"Core",
					"CoreUObject",
					"EditorStyle",
					"EditorWidgets",
					"Engine",
					"InputCore",
					"LevelEditor",
					"MainFrame",
					"MaterialEditor",
					"MediaAssets",
					"MediaFrameworkUtilities",
					"MediaIOCore",
                    "MediaPlayerEditor",
                    "MediaUtils",
					"PlacementMode",
					"PropertyEditor",
					"Slate",
					"SlateCore",
					"TimeManagement",
					"UnrealEd",
					"WorkspaceMenuStructure",
				});
		}
	}
}
