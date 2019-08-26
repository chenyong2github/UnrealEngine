// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioModulationEditor : ModuleRules
{
	public AudioModulationEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		OptimizeCode = CodeOptimization.Never;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"PropertyEditor",
				"SequenceRecorder",
				"Slate",
				"SlateCore",
				"AudioEditor",
				"AudioModulation",
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
