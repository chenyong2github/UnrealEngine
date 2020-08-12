// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MetasoundEditor : ModuleRules
	{
		public MetasoundEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			OptimizeCode = CodeOptimization.Never;

			PrivateDependencyModuleNames.AddRange
			(
				new string[]
				{
					"AudioExtensions",
					"AudioMixer",
					"MetasoundEngine",
					"MetasoundFrontend",
					"MetasoundGraphCore",
					"ToolMenus",
				}
			);

			PublicDependencyModuleNames.AddRange
			(
				new string[]
				{
					"ApplicationCore",
					"AudioMixer",
					"ClassViewer",
					"ContentBrowser",
					"Core",
					"CoreUObject",
					"DetailCustomizations",
					"EditorStyle",
					"Engine",
					"GraphEditor",
					"InputCore",
					"PropertyEditor",
					"RenderCore",
					"Slate",
					"SlateCore",
					"EditorFramework",
					"UnrealEd",
				}
			);
		}
	}
}
