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
				"Engine",
				"PropertyEditor",
				"InputCore",
				"Slate",
				"UMG",
				"SlateCore",
				"CommonUI",
                "EditorWidgets",
				"EditorStyle",
				"GameplayTags",
				"GameplayTagsEditor",
            }
        );

		PrivateIncludePaths.AddRange(
			new string[]
			{
				"CommonUI/Private",
                "CommonUIEditor/Private",
			}
		);

		PublicIncludePaths.AddRange(
			new string[]
			{
			}
		);
	}
}
