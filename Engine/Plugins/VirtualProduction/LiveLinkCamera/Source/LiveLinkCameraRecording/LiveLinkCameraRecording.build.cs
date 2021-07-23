// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LiveLinkCameraRecording : ModuleRules
{
	public LiveLinkCameraRecording(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
			}
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CameraCalibrationCore",
				"Core",
				"CoreUObject",
				"LevelSequence",
				"LiveLinkCamera",
				"LiveLinkComponents",
				"LiveLinkInterface",
				"LiveLinkMovieScene",
				"LiveLinkSequencer",
				"MovieScene",
				"MovieSceneTools",
				"MovieSceneTracks",
				"Sequencer",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"TakeTrackRecorders",
			}
		);
	}
}
