// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class EditorMenus : ModuleRules
	{
		public EditorMenus(ReadOnlyTargetRules Target) : base(Target)
		{
			//bFasterWithoutUnity = true;
			//PCHUsage = PCHUsageMode.NoPCHs;

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Slate",
					"SlateCore",
					"EditorSubSystem",
					"Engine", // For Subsystems
					"UnrealEd",
				}
			);

			//PrivateIncludePathModuleNames.AddRange(
			//	new string[] {
			//	"UnrealEd",
			//	});
		}
	}
}
