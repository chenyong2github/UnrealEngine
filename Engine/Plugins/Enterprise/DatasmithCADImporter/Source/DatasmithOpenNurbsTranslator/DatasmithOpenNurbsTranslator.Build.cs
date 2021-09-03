// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using System;

namespace UnrealBuildTool.Rules
{
	public class DatasmithOpenNurbsTranslator : ModuleRules
	{
		public DatasmithOpenNurbsTranslator(ReadOnlyTargetRules Target) : base(Target)
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
					"DatasmithCore",
					"DatasmithContent",
					"DatasmithTranslator",
					"Engine",
					"MeshDescription",
					"ParametricSurface",
					"StaticMeshDescription",
				}
			);

			if (System.Type.GetType("OpenNurbs6") != null)
			{
				PrivateDependencyModuleNames.Add("OpenNurbs6");
			}

            if (System.Type.GetType("CoreTech") != null)
            {
				PrivateDependencyModuleNames.Add("CoreTech");
            }

            if (Target.Type == TargetType.Editor)
            {
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
	 					"MessageLog",
	 					"UnrealEd",
					}
				);
			}

		}
    }
}
