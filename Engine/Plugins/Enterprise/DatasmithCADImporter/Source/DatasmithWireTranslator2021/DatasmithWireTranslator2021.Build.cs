// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class DatasmithWireTranslator2021 : ModuleRules
{
	public DatasmithWireTranslator2021(ReadOnlyTargetRules Target) : base(Target)
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

		if (System.Type.GetType("OpenModel2021") != null)
		{
			PrivateDependencyModuleNames.Add("OpenModel2021");
		}

		if (System.Type.GetType("CoreTech") != null)
		{
			PrivateDependencyModuleNames.Add("CoreTech");
		}
	}
}
