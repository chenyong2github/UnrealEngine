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
				"GeometricObjects",
				"PROJ",
				"Projects",
				"Slate",
				"SlateCore"
			}
		);

		// Stage Proj data files
		string ProjRedistFolder = Path.Combine(PluginDirectory, @"Resources\PROJ\*");
		RuntimeDependencies.Add(ProjRedistFolder, StagedFileType.NonUFS);

		// Add dependencies to PROJ
		List<string> RuntimeModuleNames = new List<string>();
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			foreach (string RuntimeModuleName in RuntimeModuleNames)
			{
				string ModulePath = Path.Combine(ProjRedistFolder, RuntimeModuleName);
				if (!File.Exists(ModulePath))
				{
					string Err = string.Format("PROJ SDK module '{0}' not found.", ModulePath);
					System.Console.WriteLine(Err);
					throw new BuildException(Err);
				}
				RuntimeDependencies.Add("$(BinaryOutputDir)/" + RuntimeModuleName, ModulePath);
			}
		}
	}
}
