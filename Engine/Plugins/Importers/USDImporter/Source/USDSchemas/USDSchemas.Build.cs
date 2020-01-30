// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDSchemas : ModuleRules
	{
		public USDSchemas(ReadOnlyTargetRules Target) : base(Target)
		{
			bUseRTTI = true;

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CinematicCamera",
					"Core",
					"CoreUObject",
					"EditorStyle",
					"Engine",
					"MaterialEditor",
					"MeshDescription",
					"MeshDescriptionOperations",
					"MeshUtilities",
					"MessageLog",
					"PropertyEditor",
					"StaticMeshDescription",
					"UnrealUSDWrapper",
					"USDUtilities",
					"UnrealEd",
				}
				);
		}
	}
}
