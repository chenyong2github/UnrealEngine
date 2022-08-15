// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MetasoundEditor : ModuleRules
	{
		public MetasoundEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange
			(
				new string[]
				{
					"AppFramework",
					"AudioExtensions",
					"AudioMixer",
					"AudioWidgets",
					"AudioSynesthesia",
					"EditorWidgets",
					"Kismet",
					"KismetWidgets",
					"MetasoundEngine",
					"MetasoundFrontend",
					"MetasoundGenerator",
					"MetasoundGraphCore",
					"SignalProcessing",
					"ToolMenus",
					"ToolWidgets",
					"WaveTable",
					"WaveTableEditor"
				}
			);

			PublicDependencyModuleNames.AddRange
			(
				new string[]
				{
					"ApplicationCore",
					"AudioAnalyzer",
					"AudioMixer",
					"AudioWidgets",
					"ClassViewer",
					"ContentBrowser",
					"Core",
					"CoreUObject",
					"DetailCustomizations",
					"EditorFramework",
					"EditorStyle",
					"Engine",
					"GraphEditor",
					"InputCore",
					"PropertyEditor",
					"RenderCore",
					"Slate",
					"SlateCore",
					"UnrealEd"
				}
			);
		}
	}
}
