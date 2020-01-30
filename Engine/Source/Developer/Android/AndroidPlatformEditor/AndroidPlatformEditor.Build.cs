// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AndroidPlatformEditor : ModuleRules
{
	public AndroidPlatformEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		BinariesSubFolder = "Android";

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"InputCore",
				"Engine",
				"Slate",
				"SlateCore",
				"EditorStyle",
                "EditorWidgets",
                "DesktopWidgets",
				"PropertyEditor",
				"SharedSettingsWidgets",
				"SourceControl",
				"AndroidRuntimeSettings",
                "AndroidDeviceDetection",
                "TargetPlatform",
                "RenderCore",
                "RHI",
                "MaterialShaderQualitySettings",
				"MainFrame",
                "AudioSettingsEditor"
            }
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Settings",
			}
		);
	}
}
