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
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Analytics",
                    "Core",
                    "CoreUObject",
                    "DatasmithCore",

                    "Engine",
                    "MainFrame",
					"MaterialEditor",
					"MeshUtilities",
                    "MeshDescription",
                    "MessageLog",
					"Projects",
					"RawMesh",
					"StaticMeshDescription",
                    "UnrealEd",
                    "XmlParser",
                    "DatasmithDispatcher",

                    "CADInterfaces",
                    "CADLibrary",
					"CADTools",
					"DatasmithCADTranslator",
                    "DatasmithTranslator"
                }
            );


			PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "DatasmithContent",
                    "DatasmithImporter"
                }
            );
        }
    }
}
