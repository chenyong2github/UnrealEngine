// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CommonUI : ModuleRules
{
	public CommonUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
                "Engine",
				"InputCore",
				"Slate",
                "UMG",
                "WidgetCarousel",
                "CommonInput",
				"MediaAssets",
				"GameplayTags"
            }
		);

		PrivateDependencyModuleNames.AddRange(
		new string[]
			{
				"SlateCore",
                "Analytics",
				"AnalyticsET",
                "EngineSettings",
				"AudioMixer"
            }
		);

        PrivateIncludePaths.AddRange(
			new string[]
			{
                "CommonUI/Private",
			}
		);

		PublicIncludePaths.AddRange(
			new string[]
			{
			}
		);

        if (Target.Type == TargetType.Editor)
        {
            PublicDependencyModuleNames.AddRange(
                new string[] {
                    "UnrealEd",
                }
            );
        }
    }
}
