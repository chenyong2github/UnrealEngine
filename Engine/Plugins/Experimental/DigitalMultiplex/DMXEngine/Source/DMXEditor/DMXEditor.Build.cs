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
				"DMXProtocolArtNet",
				"DMXProtocolSACN",
				"DMXRuntime",
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
				"UnrealEd",
				"EditorStyle",
				"PropertyEditor",
				"KismetWidgets",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"Json",
				"Projects",
				"MainFrame",
				"XmlParser"
			}
		);
	}
}