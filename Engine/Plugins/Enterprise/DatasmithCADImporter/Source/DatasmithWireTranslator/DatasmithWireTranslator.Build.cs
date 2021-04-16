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
                "CADLibrary",
                "CADTools",
                "DatasmithContent",
				"DatasmithCore",
				"DatasmithCoreTechParametricSurfaceData",
				"DatasmithTranslator",
				"Engine",
				"MeshDescription",
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

		if (System.Type.GetType("OpenModel") != null)
		{
			PrivateDependencyModuleNames.Add("OpenModel");
		}

		if (System.Type.GetType("CoreTech") != null)
		{
			PrivateDependencyModuleNames.Add("CoreTech");
		}
	}
}
