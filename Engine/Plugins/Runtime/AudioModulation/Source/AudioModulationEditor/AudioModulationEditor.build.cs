// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioModulationEditor : ModuleRules
{
	public AudioModulationEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"GameProjectGeneration",
				"UnrealEd",
				"PropertyEditor",
				"SequenceRecorder",
				"Slate",
				"SlateCore",
				"InputCore",
				"EditorStyle",
				"AudioEditor",
				"AudioExtensions",
				"AudioModulation",
				"CurveEditor",
				"EditorWidgets"
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"AudioModulationEditor/Private",
				"AudioModulation/Private"
			}
		);
	}
}
