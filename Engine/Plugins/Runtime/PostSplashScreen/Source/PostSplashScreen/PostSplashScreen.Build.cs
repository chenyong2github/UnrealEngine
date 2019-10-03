// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// This module must be loaded "PreLoadingScreen" in the .uproject file, otherwise it will not hook in time!

public class PostSplashScreen : ModuleRules
{
    public PostSplashScreen(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivatePCHHeaderFile = "Public/PostSplashScreenPrivatePCH.h";

        PrivateDependencyModuleNames.AddRange(
			new string[] {
                "Core",
                "RHI",
                "RenderCore",
                "Slate",
                "SlateCore",
                "Projects",
                "Engine",
                "RenderCore",
                "ApplicationCore",
                "PreLoadScreen"
            }
		);
    }
}
