// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SmartObjectsEditorModule : ModuleRules
	{
		public SmartObjectsEditorModule(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicIncludePaths.AddRange(
			new string[] {
			}
			);

			PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"AssetTools",
				"EditorFramework",
				"UnrealEd",
				"SourceControl",
				"AIModule",
				"SmartObjectsModule",
				"MassEntity"
			}
			);

			PrivateDependencyModuleNames.AddRange(
			new string[] {
				"RenderCore",
			}
			);
		}

	}
}
