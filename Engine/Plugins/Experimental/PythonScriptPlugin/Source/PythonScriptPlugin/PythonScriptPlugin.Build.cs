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
				}
			);

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[] {
						"DesktopPlatform",
						"EditorStyle",
						"LevelEditor",
						"UnrealEd",
						"EditorSubsystem",
					}
				);
			}

			// some of the symbols libpython.a is referencing are unresolved (see UE-70768)
			bIgnoreUnresolvedSymbols = true;
		}
	}
}
