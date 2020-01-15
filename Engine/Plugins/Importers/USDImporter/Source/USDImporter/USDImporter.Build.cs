// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDImporter : ModuleRules
	{
		public USDImporter(ReadOnlyTargetRules Target) : base(Target)
        {
			// We require the whole editor to be RTTI enabled on Linux for now
			if (Target.Platform != UnrealTargetPlatform.Linux)
			{
				bUseRTTI = true;
			}

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
                    "RawMesh",
                    "GeometryCache",
					"MeshDescription",
					"MeshDescriptionOperations",
					"MeshUtilities",
					"MessageLog",
					"PythonScriptPlugin",
                    "RenderCore",
                    "RHI",
					"StaticMeshDescription",
					"UnrealUSDWrapper",
					"USDUtilities",
                }
				);
		}
	}
}
