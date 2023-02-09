// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Underscore : ModuleRules
{
	public Underscore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
			}
			);
				
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"AudioMixer",
				"GameplayTags",
				"StateTreeModule",
				"AudioExtensions",

			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
			}
			);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
				"UnrealEd",
				}
				);
		}
	}
}
