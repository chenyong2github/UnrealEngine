// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class RenderTrace : ModuleRules
{
	public RenderTrace(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				"Private",
                //"../Shaders/Shared"
            }
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Renderer",
				"TargetPlatform",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"Projects",
				"RHI",
				"RenderCore", 
				"Renderer",
			}
		);
		SetupModulePhysicsSupport(Target);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"EditorFramework",
					"MaterialUtilities", 
					"Slate",
					"SlateCore",
					"UnrealEd",
				}
			);
		}
	}
}
