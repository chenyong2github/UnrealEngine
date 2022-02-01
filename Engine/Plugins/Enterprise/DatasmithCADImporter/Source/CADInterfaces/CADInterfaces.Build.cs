// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class CADInterfaces : ModuleRules
	{
		public CADInterfaces(ReadOnlyTargetRules Target) : base(Target)
		{
			bLegalToDistributeObjectCode = true;
			bUseUnity = false;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CADKernel",
					"CADLibrary",
					"CADTools",
					"DatasmithCore",
					"Json",
				}
			);

			// CAD library is only available if CoreTech is available too
			bool bHasCoretech = System.Type.GetType("CoreTech") != null;
			bool bHasTechSoft = System.Type.GetType("TechSoft") != null;

			// Support for Windows only
			bool bIsPlateformSupported = Target.Platform == UnrealTargetPlatform.Win64;

			if (bIsPlateformSupported && bHasCoretech)
			{
				PublicDependencyModuleNames.Add("CoreTech");
			}

			if (bIsPlateformSupported && bHasTechSoft)
			{
				PublicDependencyModuleNames.Add("TechSoft");
			}
		}
	}
}
