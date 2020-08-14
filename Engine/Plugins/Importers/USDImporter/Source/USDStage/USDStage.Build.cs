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
					"Core",
					"CoreUObject",
					"EditorFramework",
					"EditorStyle",
					"Engine",
					"LevelSequence",
					"MeshDescription",
					"MeshUtilities",
					"MessageLog",
					"MovieScene",
					"MovieSceneTracks",
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"UnrealEd",
					"UnrealUSDWrapper",
					"USDSchemas",
					"USDUtilities",
				});
		}
	}
}
