// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DMXEditor : ModuleRules
{
	public DMXEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"DMXProtocol",
				"DMXProtocolEditor",
				"DMXRuntime",
				"ToolMenus"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"AssetRegistry",
				"AssetTools",
				"CoreUObject",
				"Kismet",
				"EditorFramework",
				"UnrealEd",
				"EditorStyle",
				"PropertyEditor",
				"KismetWidgets",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"Json",
				"JsonUtilities",
				"Projects",
				"MainFrame",
				"XmlParser",
				"Sequencer",
				"MovieScene",
				"TakesCore",
				"TakeRecorder",
				"TakeTrackRecorders",
				"ContentBrowser"
			}
		);
	}
}