// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ImagePlateEditor : ModuleRules
	{
		public ImagePlateEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetTools",
					"Core",
					"CoreUObject",
					"EditorStyle",
					"Engine",
					"ImagePlate",
					"LevelSequence",
					"MovieScene",
					"MovieSceneTools",
					"MovieSceneTracks",
					"RHI",
					"Sequencer",
					"Slate",
					"SlateCore",
                    "TimeManagement",
					"UnrealEd",
				}
			);
		}
	}
}
