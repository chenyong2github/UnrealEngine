// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
    public class DatasmithPLMXMLTranslator : ModuleRules
    {
        public DatasmithPLMXMLTranslator(ReadOnlyTargetRules Target) : base(Target)
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
                    "Engine",
                    "MeshDescription",
					"XmlParser",

					"DatasmithCore",

					"CADInterfaces",
					"CADLibrary",
					"CADTools",
					"DatasmithCoreTechParametricSurfaceData",
					"DatasmithCADTranslator", // for DatasmithMeshBuilder
                    "DatasmithTranslator",
                    "DatasmithDispatcher",

                    "DatasmithContent",
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

			if (System.Type.GetType("CoreTech") != null)
            {
	            PrivateDependencyModuleNames.Add("CoreTech");
            }
        }
    }
}
