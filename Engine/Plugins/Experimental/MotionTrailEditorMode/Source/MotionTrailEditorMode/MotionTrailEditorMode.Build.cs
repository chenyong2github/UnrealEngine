// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MotionTrailEditorMode : ModuleRules
{
	public MotionTrailEditorMode(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "Slate",
                "SlateCore",
                "EditorStyle"
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"UnrealEd",
				"LevelEditor",
				"AppFramework",
				
                "EditorInteractiveToolsFramework",
                "InteractiveToolsFramework",
                "ViewportInteraction",

                "MovieScene",
                "MovieSceneTracks",
                "MovieSceneTools",
                "Sequencer",
				"LevelSequence",

                "ControlRig"
			}
        );
	}
}
