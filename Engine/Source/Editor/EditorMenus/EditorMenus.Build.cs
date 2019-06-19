// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class EditorMenus : ModuleRules
	{
		public EditorMenus(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"ApplicationCore",
					"Slate",
					"SlateCore",
					"EditorSubSystem",
					"Engine",
					"UnrealEd",
				}
			);
		}
	}
}
