// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class LiveLinkSequencer : ModuleRules
	{
		public LiveLinkSequencer(ReadOnlyTargetRules Target) : base(Target)
        {
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"LiveLinkInterface",
					"MovieScene",
					"SerializedRecorderInterface",
				    "TakeTrackRecorders",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"HeadMountedDisplay",
					"EditorStyle",
					"LiveLink",
					"LiveLinkMovieScene",
					"MovieSceneTools",
					"MovieSceneTracks",
					"Projects",
					"Sequencer",
					"SequenceRecorder",
					"Slate",
					"SlateCore",
					"TakesCore",
					"TakeRecorder",
					"TargetPlatform",
					"UnrealEd",
                }
			); 
		}
	}
}
