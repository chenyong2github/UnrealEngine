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
				"Renderer",
				"RHI"
			});

		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"DisplayClusterProjection"
			});

		PrivateIncludePaths.AddRange(
			new string[] {
				"../../../../Source/Runtime/Renderer/Private"
			});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
