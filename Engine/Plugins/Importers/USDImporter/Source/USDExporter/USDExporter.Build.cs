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
					"UnrealUSDWrapper"
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"JsonUtilities",
					"EditorFramework",
					"UnrealEd",
					"InputCore",
					"SlateCore",
                    "PropertyEditor",
					"Slate",
                    "EditorStyle",
                    "RawMesh",
                    "GeometryCache",
					"MeshDescription",
					"MeshUtilities",
					"MessageLog",
					"PythonScriptPlugin",
                    "RenderCore",
                    "RHI",
					"USDUtilities",
                }
				);
		}
	}
}
