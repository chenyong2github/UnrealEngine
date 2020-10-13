// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class PicpProjection : ModuleRules
{
	public PicpProjection(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Composure",
				"CinematicCamera",
				"DisplayCluster",
				"DisplayClusterProjection",
				"MPCDI",
				"PicpMPCDI",
				"RenderCore",
				"RHI"
			});

		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"DisplayClusterProjection"
			});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
