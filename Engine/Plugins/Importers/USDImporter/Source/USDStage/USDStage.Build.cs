// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDStage : ModuleRules
	{
		public USDStage(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CinematicCamera",
					"Core",
					"CoreUObject",
					"Engine",
					"LevelSequence",
					"MeshDescription",
					"MovieScene",
					"MovieSceneTracks",
					"Slate",
					"SlateCore",
					"Projects", // So that we can use the plugin manager to find out our content dir and cook the master materials
					"StaticMeshDescription",
					"UnrealUSDWrapper",
					"USDClasses",
					"USDSchemas",
					"USDUtilities",
				}
			);

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"LevelSequenceEditor",
						"Sequencer",
						"UnrealEd",
					}
				);
			}
		}
	}
}
