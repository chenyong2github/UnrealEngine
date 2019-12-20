// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDExporter : ModuleRules
	{
		public USDExporter(ReadOnlyTargetRules Target) : base(Target)
        {
			// We require the whole editor to be RTTI enabled on Linux for now
			if (Target.Platform != UnrealTargetPlatform.Linux)
			{
				bUseRTTI = true;
			}

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
