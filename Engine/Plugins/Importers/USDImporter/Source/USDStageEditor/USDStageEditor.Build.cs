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
					"EditorStyle",
					"Engine",
					"InputCore",
					"LevelEditor",
					"Projects", // So that we can use the IPluginManager, required for our custom style
					"Slate",
					"SlateCore",
					"UnrealEd",
					"UnrealUSDWrapper",
					"USDSchemas",
					"USDStageImporter",
					"USDStage",
					"USDStageEditorViewModels",
					"USDUtilities",
					"WorkspaceMenuStructure",
				}
			);
		}
	}
}
