// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PicpMPCDI : ModuleRules
{
	public PicpMPCDI(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"PicpProjection"
			});

		PrivateIncludePaths.AddRange(
			new string[] {
				"MPCDI/Private"
			});

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"MPCDI",
				"Projects",
				"RenderCore",
				"RHI"
			});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
