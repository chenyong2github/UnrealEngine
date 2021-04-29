// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

/** Shared functionality between RemoteControlUI and RemoteControlProtocolWidgets */
public class RemoteControlCommon : ModuleRules
{
	public RemoteControlCommon(ReadOnlyTargetRules Target) : base(Target)
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
				"EditorSubsystem",
				"Engine",
				"InputCore",
				"Projects",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"UnrealEd",
			}
		);
    }
}
