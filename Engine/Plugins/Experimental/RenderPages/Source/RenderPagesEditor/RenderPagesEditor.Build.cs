// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RenderPagesEditor : ModuleRules
{
	public RenderPagesEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RemoteControl",
			}
		);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetTools",
				"DesktopPlatform",
				"EditorStyle",
				"GraphEditor",
				"InputCore",
				"Kismet",
				"LevelSequence",
				"MovieRenderPipelineCore",
				"MovieScene",
				"Projects",
				"PropertyEditor",
				"RenderPages",
				"RenderPagesDeveloper",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd",
			}
		);
	}
}