// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class TimeSynthEditor : ModuleRules
	{
        public TimeSynthEditor(ReadOnlyTargetRules Target) : base(Target)
		{
            PublicDependencyModuleNames.AddRange(
				new string[] {
                    "Core",
					"CoreUObject",
					"Engine",
                    "UnrealEd",
					"AudioEditor",
                    "TimeSynth",
					"AudioMixer",
                    "EditorStyle",
                    "Slate",
                    "SlateCore",
					"ContentBrowser",
					"ToolMenus",
				}
			);

            PrivateIncludePathModuleNames.AddRange(
            new string[] {
                    "AssetTools"
            });
        }
	}
}