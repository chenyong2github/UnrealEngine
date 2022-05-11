// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class EvalGraphEditor : ModuleRules
	{
        public EvalGraphEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("EvalGraphEditor/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
				    "Slate",
				    "SlateCore",
				    "Engine",
					"EditorFramework",
					"UnrealEd",
				    "PropertyEditor",
				    "RenderCore",
				    "RHI",
				    "AssetTools",
				    "AssetRegistry",
				    "SceneOutliner",
					"EditorStyle",
					"AssetTools",
					"ToolMenus",
					"LevelEditor",
					"InputCore",
					"AdvancedPreviewScene",
					"GraphEditor",
					"EvalGraphCore",
					"EvalGraphEngine",
					"Slate",
				}
			);
		}
	}
}
