// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
	public class PythonScriptPlugin : ModuleRules
	{
		public PythonScriptPlugin(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Analytics",
					"Projects",
					"Python",
					"Slate",
					"SlateCore",
					"InputCore",
					"Sockets",
					"Networking",
					"Json",
				}
			);

			if (Target.bBuildEditor == true)
			{
				PublicDependencyModuleNames.AddRange(
					new string[] {
						"ToolMenus"
					}
				);

				PrivateDependencyModuleNames.AddRange(
					new string[] {
						"DesktopPlatform",
						"EditorStyle",
						"LevelEditor",
						"UnrealEd",
						"EditorSubsystem",
						"BlueprintGraph",
					}
				);
			}
		}
	}
}
