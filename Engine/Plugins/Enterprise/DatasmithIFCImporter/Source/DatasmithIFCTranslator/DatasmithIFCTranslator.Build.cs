// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
    public class DatasmithIFCTranslator : ModuleRules
    {
        public DatasmithIFCTranslator(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Analytics",
                    "Core",
                    "CoreUObject",
                    "DatasmithCore",
					"Engine",
					"Json",
                    "MainFrame",
					"MaterialEditor",
					"MeshUtilities",
                    "MeshDescription",
                    "MessageLog",
					"Projects",
					"RawMesh",
                    "Slate",
                    "SlateCore",
					"StaticMeshDescription",
                    "UnrealEd",
                    "XmlParser",
                }
            );

			string IfcEngineDir = Path.Combine(PluginDirectory, "Source", "ThirdParty", "NotForLicensees", "ifcengine");
			if (Directory.Exists(IfcEngineDir))
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"IFCEngine",
					}
				);
			}

			PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "DatasmithContent",
                    "DatasmithImporter"
                }
            );

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				RuntimeDependencies.Add("$(EngineDir)/Plugins/Enterprise/DatasmithIFCImporter/Binaries/Win64/ifcengine.dll");
			}
		}
	}
}
