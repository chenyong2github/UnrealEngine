// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PUMAEditor : ModuleRules
	{
		public PUMAEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
			new string[]
			{
                "AnimGraph",
                "AnimationCore",
                "Core",
                "CoreUObject",
                "Engine",
				"PUMARuntime",
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
