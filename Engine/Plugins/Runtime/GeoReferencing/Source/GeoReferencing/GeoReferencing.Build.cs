// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

using System;
using System.IO;
using System.Collections.Generic;

public class GeoReferencing : ModuleRules
{
	public GeoReferencing(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"GeometryCore",
				"PROJ",
				"Projects",
				"Slate",
				"SlateCore",
				"SQLiteCore"
			}
		);

		string ProjRedistFolder = Path.Combine(PluginDirectory, @"Resources\PROJ\*");
		RuntimeDependencies.Add(ProjRedistFolder, StagedFileType.UFS);
	}
}
