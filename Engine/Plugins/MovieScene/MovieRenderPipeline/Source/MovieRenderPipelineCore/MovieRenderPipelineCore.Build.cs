// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MovieRenderPipelineCore : ModuleRules
{
	public MovieRenderPipelineCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"InputCore",
				"RenderCore",
				"RHI",
				"UMG",
				"Landscape", // To flush grass
				"AudioMixer",
				"NonRealtimeAudioRenderer",
				"Sockets", 
				"Networking",
				"Http",
				"DeveloperSettings",
				"CinematicCamera"

			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"SlateCore",
                "MovieScene",
                "MovieSceneTracks",
				"LevelSequence",
				"Engine",
				"ImageWriteQueue", // For debug tile writing
				"OpenColorIO",
			}
		);

		if (Target.bBuildEditor == true)
        {
			PublicDependencyModuleNames.Add("MovieSceneTools");
        }
	}
}
