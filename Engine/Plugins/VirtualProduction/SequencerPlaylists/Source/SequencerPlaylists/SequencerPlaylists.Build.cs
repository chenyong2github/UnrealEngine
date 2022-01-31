// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SequencerPlaylists : ModuleRules
{
	public SequencerPlaylists(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[] {
		});

		PrivateIncludePaths.AddRange(new string[] {
		});

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Projects",
			"InputCore",
			"EditorFramework",
			"UnrealEd",
			"ToolMenus",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"WorkspaceMenuStructure",
			"LevelSequence",
			"TakeRecorder",
			"PropertyEditor",
			"EditorStyle",
			"MovieScene",
			"LevelSequenceEditor",
			"TakesCore",
			"ToolWidgets",
			"MovieSceneTools",
		});

		DynamicallyLoadedModuleNames.AddRange(new string[] {
		});
	}
}
