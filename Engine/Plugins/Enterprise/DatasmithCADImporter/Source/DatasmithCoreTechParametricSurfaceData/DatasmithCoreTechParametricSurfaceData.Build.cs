// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class DatasmithCoreTechParametricSurfaceData : ModuleRules
	{
		public DatasmithCoreTechParametricSurfaceData(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"MeshDescription",
					"DatasmithContent",
					"DatasmithTranslator",
					"Engine",
					"StaticMeshDescription",
					"CADInterfaces",
					"CADLibrary",
				}
			);
		}
	}
}
