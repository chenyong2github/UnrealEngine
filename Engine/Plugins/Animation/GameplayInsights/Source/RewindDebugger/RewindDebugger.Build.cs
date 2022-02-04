// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class RewindDebugger : ModuleRules
	{
		public RewindDebugger(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ActorPickerMode",
				"AnimationBlueprintEditor",
				"ApplicationCore",
				"AssetRegistry",
				"Core",
				"CoreUObject",
				"EditorFramework",
				"EditorStyle",
				"EditorWidgets",
				"Engine",
				"GameplayInsights",
				"GameplayInsightsEditor",
				"InputCore",
				"Kismet",
				"LevelEditor",
				"Persona",
				"SceneOutliner",
				"SequencerWidgets",
				"SlateCore",
				"Slate",
				"ToolMenus",
				"TraceLog",
				"TraceAnalysis",
				"TraceServices",
				"TraceInsights",
				"UnrealEd",
				"RewindDebuggerInterface",
				"ToolWidgets",
				"MainFrame"
			});
		}
	}
}

