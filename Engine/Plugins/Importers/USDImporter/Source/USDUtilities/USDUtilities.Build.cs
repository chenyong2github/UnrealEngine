// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDUtilities : ModuleRules
	{
		public USDUtilities(ReadOnlyTargetRules Target) : base(Target)
		{
			bUseRTTI = true;

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CinematicCamera",
					"Core",
					"CoreUObject",
					"DesktopPlatform",
					"Engine",
					"UnrealEd",
					"InputCore",
					"SlateCore",
					"PropertyEditor",
					"Slate",
					"EditorStyle",
					"RawMesh",
					"GeometryCache",
					"MaterialEditor",
					"MeshDescription",
					"MeshUtilities",
					"MikkTSpace",
					"PythonScriptPlugin",
					"RenderCore",
					"RHI",
					"StaticMeshDescription",
					"MessageLog",
					"JsonUtilities",
				}
				);

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PrivateDependencyModuleNames.Add("UnrealUSDWrapper");
			}
		}
	}
}
