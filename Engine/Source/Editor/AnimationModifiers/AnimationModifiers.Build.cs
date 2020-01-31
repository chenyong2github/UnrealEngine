// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AnimationModifiers : ModuleRules
{
	public AnimationModifiers(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] 
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "Slate",
                "SlateCore",
                "EditorStyle",
                "UnrealEd",
                "MainFrame",
                "PropertyEditor",
                "Kismet",
                "AssetTools",
                "ClassViewer",
                "AnimationEditor",
                "SkeletonEditor",
                "AssetRegistry"                
            }
		);
    }
}
