// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DataprepEditor : ModuleRules
	{
		public DataprepEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AdvancedPreviewScene",
					"ApplicationCore",
					"AssetRegistry",
					"AssetTools",
					"BlueprintGraph",
					"Core",
					"CoreUObject",
					"DataprepCore",
					"DesktopPlatform",
					"EditorStyle",
					"EditorWidgets",
					"EditorWidgets",
					"Engine",
					"GraphEditor",
					"InputCore",
					"Kismet",
					"KismetCompiler",
					"KismetWidgets",
					"MeshDescription",
					"MeshDescriptionOperations",
					"MeshUtilities",
					"MeshUtilitiesCommon",
					"MessageLog",
					"Projects",
					"PropertyEditor",
					"RHI",
					"SceneOutliner",
					"Slate",
					"SlateCore",
					"StatsViewer",
					"UnrealEd",
				}
			);
		}
	}
}
