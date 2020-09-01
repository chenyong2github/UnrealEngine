// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ChaosCachingEditor : ModuleRules
	{
        public ChaosCachingEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("ChaosCachingEditor/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Slate",
					"SlateCore",
					"InputCore",
					"Engine",
					"UnrealEd",
					"EditorStyle",
					"PropertyEditor",
					"BlueprintGraph",
					"ToolMenus",
					"PhysicsCore",
					"ChaosCaching"
				});

			SetupModulePhysicsSupport(Target);
			PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
		}
	}
}
