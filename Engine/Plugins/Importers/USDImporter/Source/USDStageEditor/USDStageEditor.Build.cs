// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDStageEditor : ModuleRules
	{
		public USDStageEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"DesktopWidgets",
					"EditorFramework",
					"EditorStyle",
					"Engine",
					"InputCore",
					"LevelEditor",
					"Projects", // So that we can use the IPluginManager, required for our custom style
					"SceneOutliner",
					"Slate",
					"SlateCore",
					"UnrealEd",
					"UnrealUSDWrapper",
					"USDSchemas",
					"USDStage",
					"USDStageEditorViewModels",
					"USDStageImporter",
					"USDUtilities",
					"WorkspaceMenuStructure",
				}
			);
		}
	}
}
