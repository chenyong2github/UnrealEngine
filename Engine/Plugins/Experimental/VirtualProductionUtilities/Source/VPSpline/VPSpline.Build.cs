// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VPSpline : ModuleRules
{
	public VPSpline(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CinematicCamera",
				"CoreUObject",
				"Engine",
				"LevelSequence",
				"MovieScene",
				"MovieSceneTracks",
				"Slate",
				"SlateCore",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"LevelSequenceEditor",
					"Sequencer",
					"MovieSceneTools",
					"LevelEditor",
					"EditorFramework",
					"UnrealEd",
					"EditorScriptingUtilities",
					"VPUtilitiesEditor"
				}
			);
		}

	}
}
