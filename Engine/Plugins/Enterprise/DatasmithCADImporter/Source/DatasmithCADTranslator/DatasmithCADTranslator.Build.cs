// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class DatasmithCADTranslator : ModuleRules
	{
		public DatasmithCADTranslator(ReadOnlyTargetRules Target) : base(Target)
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
					"DatasmithCore",
					"DatasmithCoreTechExtension",
					"DatasmithContent",
					"DatasmithImporter",
					"MeshDescription",
                    "CADLibrary",
                }
			);

			if (System.Type.GetType("CoreTech") != null)
			{
				PublicDependencyModuleNames.Add("CoreTech");
			}

            //PublicDefinitions.Add("USE_CORETECH_MT_PARSER");
        }
    }
}
