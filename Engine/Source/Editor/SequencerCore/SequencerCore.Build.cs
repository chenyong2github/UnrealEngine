// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SequencerCore : ModuleRules
{
	public SequencerCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				"Editor/SequencerCore/Private",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"GraphEditor",
				"InputCore",
				"Slate",
				"SlateCore",
				"TimeManagement",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"EditorFramework",
				"EditorStyle",
				"UnrealEd",
			}
		);
	}
}
