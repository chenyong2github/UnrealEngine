// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ImgMediaEditor : ModuleRules
	{
		public ImgMediaEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"AssetTools",
					"Core",
					"CoreUObject",
					"DesktopWidgets",
					"EditorFramework",
					"EditorStyle",
					"ImageWrapper",
					"ImgMedia",
					"InputCore",
					"MediaAssets",
					"Slate",
					"SlateCore",
					"UnrealEd",
					"WorkspaceMenuStructure",
				});

			PrivateIncludePaths.AddRange(
				new string[] {
					"ImgMediaEditor/Private",
					"ImgMediaEditor/Private/Customizations",
					"ImgMediaEditor/Private/Factories",
				});

			// Are we using the engine?
			if (Target.bCompileAgainstEngine)
			{
				PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Engine",
				});
			}
		}
	}
}
