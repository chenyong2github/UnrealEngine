// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class OutputRemap : ModuleRules
{
	public OutputRemap(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PublicDefinitions.Add("OutputRemap_STATIC");

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Projects",
            });

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
                "DisplayCluster",
				"RenderCore",
				"RHI",
				"UtilityShaders"
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
