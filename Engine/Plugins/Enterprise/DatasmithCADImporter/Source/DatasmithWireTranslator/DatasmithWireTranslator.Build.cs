// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class DatasmithWireTranslator : ModuleRules
{
	public DatasmithWireTranslator(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CADInterfaces",
				"CADKernel",
				"CADKernelSurface",
				"CADLibrary",
				"CADTools",
				"CoreTechSurface",
				"DatasmithContent",
				"DatasmithCore",
				"DatasmithTranslator",
				"Engine",
				"MeshDescription",
				"ParametricSurface",
				"StaticMeshDescription",
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
 					"MessageLog",
 					"UnrealEd",
				}
			);
		}

		if (System.Type.GetType("OpenModel2021_3") != null)
		{
			PrivateDependencyModuleNames.Add("OpenModel2021_3");
		}

		if (System.Type.GetType("CoreTech") != null)
		{
			PrivateDependencyModuleNames.Add("CoreTech");
		}
	}
}
