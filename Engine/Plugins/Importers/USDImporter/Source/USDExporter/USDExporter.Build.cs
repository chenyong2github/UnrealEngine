// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDExporter : ModuleRules
	{
		public USDExporter(ReadOnlyTargetRules Target) : base(Target)
        {
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"UnrealUSDWrapper",
					"Foliage"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
                    "EditorStyle",
                    "GeometryCache",
                    "PropertyEditor",
                    "RawMesh",
                    "RenderCore",
                    "RHI",
					"CinematicCamera",
					"InputCore",
					"JsonUtilities",
					"Landscape",
					"LevelSequence",
					"MaterialBaking", // So that we can use some of the export option properties
					"MaterialUtilities",
					"MeshDescription",
					"MeshUtilities",
					"MessageLog",
					"PythonScriptPlugin",
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"UnrealEd",
					"USDClasses",
					"USDStageImporter", // For USDOptionsWindow
					"USDUtilities",
                }
			);
		}
	}
}
