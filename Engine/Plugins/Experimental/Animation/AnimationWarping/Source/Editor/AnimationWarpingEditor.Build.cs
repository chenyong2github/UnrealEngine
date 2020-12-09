// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AnimationWarpingEditor : ModuleRules
	{
		public AnimationWarpingEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
			new string[]
			{
                "AnimGraph",
                "AnimationCore",
                "Core",
                "CoreUObject",
                "Engine",
				"AnimationWarpingRuntime",
            });

			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
                "SlateCore",
            });

            if (Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.AddRange(
                    new string[]
                    {
						"EditorFramework",
                        "UnrealEd",
                        "Kismet",
                        "AnimGraph",
						"AnimGraphRuntime",
                        "BlueprintGraph",
                    }
                );
            }
        }
	}
}
