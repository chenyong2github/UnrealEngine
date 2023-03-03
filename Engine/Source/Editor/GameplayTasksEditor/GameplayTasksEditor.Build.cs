// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GameplayTasksEditor : ModuleRules
	{
        public GameplayTasksEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			OverridePackageType = PackageOverrideType.EngineDeveloper;

            PrivateDependencyModuleNames.AddRange(
                new string[]
				{
					// ... add private dependencies that you statically link with here ...
					"Core",
					"CoreUObject",
					"Engine",
					"AssetTools",
					"ClassViewer",
                    "GameplayTags",
					"GameplayTasks",
                    "InputCore",
                    "PropertyEditor",
					"Slate",
					"SlateCore",
					"BlueprintGraph",
                    "Kismet",
					"KismetCompiler",
					"GraphEditor",
					"MainFrame",
					"EditorFramework",
					"UnrealEd",
                    "EditorWidgets",
				}
			);
		}
	}
}
