// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SoundCueTemplatesEditor : ModuleRules
{
	public SoundCueTemplatesEditor(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "SoundCueTemplates",
                "ToolMenus"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "EditorStyle",
                "Engine",
                "UnrealEd",
                "Slate",
                "SlateCore"
            }
        );

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "AssetTools"
            }
        );
    }
}
