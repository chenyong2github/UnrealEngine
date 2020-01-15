// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class CADInterfaces : ModuleRules
	{
		public CADInterfaces(ReadOnlyTargetRules Target) : base(Target)
		{
			bLegalToDistributeObjectCode = true;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CADTools"
				}
			);

			// CAD library is only available if CoreTech is available too
			bool bHasCoretech = System.Type.GetType("CoreTech") != null;

			// Support for Windows only
			bool bIsPlateformSupported = Target.Platform == UnrealTargetPlatform.Win64;

			if (bIsPlateformSupported && bHasCoretech)
			{
				PublicDefinitions.Add("CAD_INTERFACE");
				PublicDependencyModuleNames.Add("CoreTech");
			}
		}
	}
}
