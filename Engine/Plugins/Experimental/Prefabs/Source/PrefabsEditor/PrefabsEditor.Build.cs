// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PrefabsEditor : ModuleRules
	{
		public PrefabsEditor(ReadOnlyTargetRules Target) : base(Target)
        {
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetTools",
					"Core",
					"CoreUObject",
					"EditorFramework",
					"Engine",
					"PrefabsUncooked",
					"Slate",
					"SlateCore",
					"TargetPlatform",
					"ToolMenus",
					"UnrealEd",
				}
			); 
		}
	}
}
