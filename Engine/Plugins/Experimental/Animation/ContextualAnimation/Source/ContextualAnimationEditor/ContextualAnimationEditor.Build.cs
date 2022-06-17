// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ContextualAnimationEditor : ModuleRules
{
	public ContextualAnimationEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"AIModule",
				"NavigationSystem",
				"ContextualAnimation",
				"MotionWarping",
				"GameplayTags"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"EditorFramework",
				"UnrealEd",
				"LevelEditor",
				"DetailCustomizations",
				"EditorStyle",
				"Projects",
				"AssetTools",
				"EditorWidgets",
				"PropertyEditor",
				"KismetWidgets",
				"AdvancedPreviewScene",
				"GraphEditor",
				"SequencerWidgets",
				"MovieScene",
				"Sequencer",
				"MovieSceneTracks",
				"MovieSceneTools",
				"ClassViewer",
				"Persona",
				"RenderCore",
				"BlueprintGraph",
				"AnimGraph"
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
