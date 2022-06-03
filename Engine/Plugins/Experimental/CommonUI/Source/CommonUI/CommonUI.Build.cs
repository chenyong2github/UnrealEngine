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
				"AudioMixer",
				"DeveloperSettings"
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
					"EditorFramework",
                    "UnrealEd",
                }
            );
        }
		PrivateDefinitions.Add("UE_COMMONUI_PLATFORM_KBM_REQUIRES_ATTACHED_MOUSE=" + (bPlatformKBMRequiresAttachedMouse ? "1" : "0"));
		PrivateDefinitions.Add("UE_COMMONUI_PLATFORM_SUPPORTS_TOUCH=" + (bPlatformSupportsTouch ? "1" : "0"));
	}

	protected virtual bool bPlatformKBMRequiresAttachedMouse { get { return false; } }
	protected virtual bool bPlatformSupportsTouch
	{
		get
		{
			// Support touch testing until touch is supported on desktop
			return Target.Platform.IsInGroup(UnrealPlatformGroup.Desktop) && Target.Configuration != UnrealTargetConfiguration.Shipping;
		}
	}
}
