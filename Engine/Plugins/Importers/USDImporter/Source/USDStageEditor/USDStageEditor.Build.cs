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
					"LevelEditor",
					"InputCore",
					"Slate",
					"SlateCore",
					"UnrealEd",
					"UnrealUSDWrapper",
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
