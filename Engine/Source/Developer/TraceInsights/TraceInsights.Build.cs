// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TraceInsights : ModuleRules
{
	public TraceInsights(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange
		(
			new string[] {
				"Developer/TraceInsights/Private",
			}
		);

		PublicDependencyModuleNames.AddRange
		(
			new string[] {
				"Core",
				"ApplicationCore",
				"InputCore",
				"RHI",
				"RenderCore",
				"Slate",
				"EditorStyle",
				"TraceLog",
				"TraceServices",
				"DesktopPlatform",
			}
		);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Engine",
				}
			);
		}

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"SlateCore",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Messaging",
				"SessionServices",
			}
		);
	}
}
