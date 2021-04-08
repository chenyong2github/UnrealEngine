// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RemoteControlUI : ModuleRules
{
	public RemoteControlUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {}
		);

        PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ApplicationCore",
				"AssetRegistry",
				"AssetTools",
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"EditorWidgets",
				"EditorStyle",
				"Engine",
				"HotReload",
				"InputCore",
				"Projects",
				"PropertyEditor",
				"RemoteControl",
				"RemoteControlProtocolWidgets",
				"SceneOutliner",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"WebRemoteControl"
			}
		);
    }
}
