// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VCamCoreEditor : ModuleRules
{
	public VCamCoreEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"Slate",
				"SlateCore",
				"CinematicCamera",
				"LiveLinkInterface",
				"VCamCore"
			}
		);
	}
}
