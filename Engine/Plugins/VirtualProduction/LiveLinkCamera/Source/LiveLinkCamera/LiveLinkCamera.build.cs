// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LiveLinkCamera : ModuleRules
{
	public LiveLinkCamera(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] 
			{
			}
		);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"CinematicCamera",
				"Core",
				"CoreUObject",
				"Engine",
				"LiveLinkInterface",
				"LiveLink",
				"LiveLinkComponents",
				"LensDistortion",
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
