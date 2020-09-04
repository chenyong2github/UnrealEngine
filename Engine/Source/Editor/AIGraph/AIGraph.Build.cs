// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AIGraph : ModuleRules
{
    public AIGraph(ReadOnlyTargetRules Target) : base(Target)
    {
		OverridePackageType = PackageOverrideType.EngineDeveloper;

		PrivateIncludePaths.AddRange(
            new string[] {
				"Editor/GraphEditor/Private",
				"Editor/Kismet/Private",
				"Editor/AIGraph/Private",
			}
        );

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
				"AssetRegistry",
				"AssetTools",
				"ContentBrowser"
			}
        );

        PrivateDependencyModuleNames.AddRange(
            new string[] {
				"Core", 
				"CoreUObject", 
				"ApplicationCore",
				"Engine", 
                "RenderCore",
                "InputCore",
				"Slate",
				"SlateCore",
                "EditorStyle",
				"UnrealEd", 
				"MessageLog", 
				"GraphEditor",
                "Kismet",
				"AnimGraph",
				"BlueprintGraph",
                "AIModule",
				"ClassViewer",
				"ToolMenus",
			}
        );

        DynamicallyLoadedModuleNames.AddRange(
            new string[] { 
				"AssetTools",
				"AssetRegistry",
				"ContentBrowser"
            }
        );
    }
}
