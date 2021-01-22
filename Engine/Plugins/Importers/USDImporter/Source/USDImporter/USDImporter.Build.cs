// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDImporter : ModuleRules
	{
		public USDImporter(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"JsonUtilities",
					"UnrealEd",
					"InputCore",
					"SlateCore",
					"PropertyEditor",
					"Slate",
					"EditorStyle",
					"GeometryCache",
					"MeshDescription",
					"MeshUtilities",
					"MessageLog",
					"PythonScriptPlugin",
					"StaticMeshDescription",
					"UnrealUSDWrapper",
					"USDClasses",
					"USDUtilities",
					"DeveloperSettings"
				}
			);
		}
	}
}
