// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class CADInterfaces : ModuleRules
	{
		public CADInterfaces(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CADTools"
				}
			);

			// Module is effectively empty without non redistributable content
			bool bHasNonRedistSources = Directory.Exists(Path.Combine(ModuleDirectory, "Public", "NotForLicensees"));

			// CAD library is only available if CoreTech is available too
			bool bHasCoretech = System.Type.GetType("CoreTech") != null;

			// Support for Windows only
			bool bIsPlateformSupported = Target.Platform == UnrealTargetPlatform.Win64;

			if (bHasNonRedistSources && bIsPlateformSupported && bHasCoretech)
			{
				PublicDefinitions.Add("CAD_INTERFACE");
				PublicDependencyModuleNames.Add("CoreTech");

				PublicIncludePaths.Add(ModuleDirectory + "/Public/NotForLicensees");
				PrivateIncludePaths.Add(ModuleDirectory + "/Private/NotForLicensees");
			}
		}
	}
}
