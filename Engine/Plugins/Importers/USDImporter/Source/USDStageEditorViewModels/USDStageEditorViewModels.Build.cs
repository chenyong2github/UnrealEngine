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
					"LevelEditor",
					"InputCore",
					"Slate",
					"SlateCore",
					"UnrealEd",
					"UnrealUSDWrapper",
					"USDStage",
					"USDStageImporter",
					"USDUtilities",
					"WorkspaceMenuStructure",
				}
				);
		}
	}
}
