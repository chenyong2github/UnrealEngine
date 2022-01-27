// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MassNavigationEditor : ModuleRules
	{
		public MassNavigationEditor(ReadOnlyTargetRules Target) : base(Target)
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
				"RHI",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"PropertyEditor",
				"MassEntity",
				"DetailCustomizations",
				"MassCommon",
				"MassNavigation",
				"MassZoneGraphNavigation",
				"ZoneGraph",
			}
			);

			PrivateDependencyModuleNames.AddRange(
			new string[] {
				"RenderCore",
				"KismetWidgets",
				"ToolMenus",
				"AppFramework",
				"Projects",
			}
			);
		}

	}
}
