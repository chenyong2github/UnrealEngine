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
				"PropertyEditor",
				"Core",
				"CoreUObject",
				"EditorWidgets",
				"EditorStyle",
				"Engine",
				"HotReload",
				"InputCore",
				"Projects",
				"RemoteControl",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"WebRemoteControl"
			}
		);
    }
}
