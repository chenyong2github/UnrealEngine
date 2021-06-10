// Copyright Epic Games, Inc. All Rights Reserved.

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
					"CADKernelSurface",
					"CADInterfaces",
					"CADLibrary",
					"CADTools",
					"CoreTechSurface",
					"DatasmithCore",
					"DatasmithContent",
					"DatasmithDispatcher",
					"DatasmithTranslator",
					"Engine",
					"MeshDescription",
					"ParametricSurface",
					"Sockets",
				}
			);

			if (System.Type.GetType("CoreTech") != null)
			{
				PublicDependencyModuleNames.Add("CoreTech");
			}
		}
	}
}
