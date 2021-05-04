// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CommonUIEditor : ModuleRules
{
	public CommonUIEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
		new string[]
			{
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"Engine",
				"PropertyEditor",
				"InputCore",
				"Slate",
				"UMG",
				"SlateCore",
				"CommonUI",
                "EditorWidgets",
				"EditorStyle",
				"UnrealEd",
				"GameplayTags",
				"GameplayTagsEditor",
				"AssetTools",
				"AssetRegistry",
				"ToolMenus",
			}
        );

		PrivateIncludePaths.AddRange(
			new string[]
			{
				"CommonUI/Private",
                "CommonUIEditor/Private",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
				"AssetTools",
			}
		);

		PublicIncludePaths.AddRange(
			new string[]
			{
			}
		);
	}
}
