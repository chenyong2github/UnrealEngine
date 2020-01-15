// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDStage : ModuleRules
	{
		public USDStage(ReadOnlyTargetRules Target) : base(Target)
		{
			// We require the whole editor to be RTTI enabled on Linux for now
			if (Target.Platform != UnrealTargetPlatform.Linux)
			{
				bUseRTTI = true;
			}

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CinematicCamera",
					"Core",
					"CoreUObject",
					"EditorStyle",
					"Engine",
					"GeometryCache",
					"InputCore",
					"JsonUtilities",
					"LevelSequence",
					"MaterialEditor",
					"MeshDescription",
					"MeshDescriptionOperations",
					"MeshUtilities",
					"MessageLog",
					"MovieScene",
					"MovieSceneTracks",
					"PropertyEditor",
					"PythonScriptPlugin",
					"RHI",
					"RenderCore",
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"USDImporter",
					"UnrealUSDWrapper",
					"USDSchemas",
					"USDUtilities",
					"UnrealEd",
				}
				);
		}
	}
}
