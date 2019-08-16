// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class PicpProjection : ModuleRules
{
	public PicpProjection(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PrivateIncludePaths.AddRange(
			new string[]
			{
				"PicpProjection/Private",
				"DisplayClusterProjection/Private",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
            });

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
                "Composure",
				"CinematicCamera",
                "DisplayCluster",
                "MPCDI",
                "PicpMPCDI",
                "RenderCore",
				"RHI",				
            }
        );

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
