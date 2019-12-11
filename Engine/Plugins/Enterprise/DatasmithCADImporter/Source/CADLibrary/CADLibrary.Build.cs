// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class CADLibrary : ModuleRules
{
	public CADLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		bLegalToDistributeObjectCode = true;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"DatasmithCore",
				"MeshDescription",
				"StaticMeshDescription"
			}
		);

		// Support for Windows only
		bool bIsPlateformSupported = Target.Platform == UnrealTargetPlatform.Win64;

		// CAD library is only available if CoreTech is available too
		bool bHasCoretech = System.Type.GetType("CoreTech") != null;

		if (bIsPlateformSupported && bHasCoretech)
		{
			PublicDefinitions.Add("CAD_LIBRARY");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"CoreTech",
					"CADInterfaces",
					"CADTools"
				}
			);
		}
	}
}
