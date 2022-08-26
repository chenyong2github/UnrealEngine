// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SourceControlWindowExtender : ModuleRules
{
	public SourceControlWindowExtender(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add("Editor/SourceControlWindowExtender/Public");

		PrivateIncludePaths.Add("Editor/SourceControlWindowExtender/Private");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core", 
				"CoreUObject", 
				"Engine",
            	"ToolMenus",
				"SlateCore",
				"Slate",
				"SourceControl",
				"SourceControlWindows",
				"UnrealEd"
			}
		);
	}
}
