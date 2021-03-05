// Copyright Epic Games, Inc. All Rights Reserved.
using System;

namespace UnrealBuildTool.Rules
{
	public class DatasmithCADRuntime : ModuleRules
	{
		public DatasmithCADRuntime(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PublicIncludePaths.Add("Runtime/Launch/Public");

			PublicDefinitions.Add("DATASMITH_CAD_IGNORE_CACHE");
			PublicDefinitions.Add("DATASMITH_CAD_RUNTIME");

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CADInterfaces",
				}
			);

			// Add CoreTech module
			if(Target.Platform == UnrealTargetPlatform.Win64)
            {
				if(System.Type.GetType("CoreTech") != null)
				{
					PublicDependencyModuleNames.Add("CoreTech");
				}
			}
		}
	}
}