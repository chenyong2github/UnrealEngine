// Copyright Epic Games, Inc. All Rights Reserved.

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
                    "CoreUObject",
                    "DatasmithCore",
					"Engine",
					"Json",
                    "MeshDescription",
                    "MessageLog",
					"Projects",
					"RawMesh",
                    "Slate",
                    "SlateCore",
					"StaticMeshDescription",
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
                    "Core",
                    "DatasmithContent",
                    "DatasmithTranslator"
                }
            );

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				RuntimeDependencies.Add("$(EngineDir)/Plugins/Enterprise/DatasmithIFCImporter/Binaries/Win64/ifcengine.dll");
				PublicDelayLoadDLLs.Add("ifcengine.dll");
			}
		}
	}
}
