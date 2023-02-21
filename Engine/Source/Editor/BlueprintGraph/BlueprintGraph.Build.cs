// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class BlueprintGraph : ModuleRules
{
	public BlueprintGraph(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "KismetCompiler",
            }
		);

		OverridePackageType = PackageOverrideType.EngineDeveloper;
		 
		PublicDependencyModuleNames.AddRange(
			new string[] { 
				"Core", 
				"CoreUObject", 
				"Engine",
                "InputCore",
				"Slate",
                
				"EditorSubsystem",
				"DeveloperSettings"
			}
		);

		PrivateDependencyModuleNames.AddRange( 
			new string[] {
				
                "KismetCompiler",
				"EditorFramework",
				"UnrealEd",
                "GraphEditor",
				"SlateCore",
                "Kismet",
                "KismetWidgets",
                "PropertyEditor",
				"ToolMenus",
				"AssetTools",
			}
		);

		CircularlyReferencedDependentModules.AddRange(
            new string[] {
                "KismetCompiler",
                "UnrealEd",
                "GraphEditor",
                "Kismet",
            }
		); 
	}
}
