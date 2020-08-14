// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PicpMPCDI : ModuleRules
{
	public PicpMPCDI(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				"PicpMPCDI/Private",
				"PicpProjection/Private",
				"MPCDI/Private"				
			});

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
				"MPCDI",
				"Projects",				
				"RenderCore",
                "RHI"
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"PicpProjection"
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("EditorFramework");
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
