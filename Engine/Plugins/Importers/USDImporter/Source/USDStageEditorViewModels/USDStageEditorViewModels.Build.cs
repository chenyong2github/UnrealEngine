// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDStageEditorViewModels : ModuleRules
	{
		public USDStageEditorViewModels(ReadOnlyTargetRules Target) : base(Target)
		{
			bUseRTTI = true;

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
					"Slate",
					"SlateCore",
					"UnrealEd",
					"UnrealUSDWrapper",
					"USDClasses",
					"USDStage",
					"USDStageImporter",
					"USDUtilities",
					"WorkspaceMenuStructure",
				}
			);
		}
	}
}
