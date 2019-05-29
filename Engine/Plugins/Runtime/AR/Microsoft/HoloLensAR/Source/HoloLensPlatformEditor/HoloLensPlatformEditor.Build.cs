// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HoloLensPlatformEditor : ModuleRules
{
	public HoloLensPlatformEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Settings",
				"TargetPlatform",
				"DesktopPlatform",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"PropertyEditor",
				"SharedSettingsWidgets",
				"AppFramework",
				"DesktopWidgets",
				"UnrealEd",
				"SourceControl",
				"WindowsTargetPlatform", // For ECompilerVersion
				"EngineSettings",
				"Projects",
				"gltfToolkit",
				"AudioSettingsEditor",
			}
		);

		PublicAdditionalLibraries.Add("crypt32.lib");
	}
}
