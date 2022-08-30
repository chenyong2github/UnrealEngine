// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class ComputeFrameworkEditor : ModuleRules
    {
        public ComputeFrameworkEditor(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateIncludePaths.AddRange(
                new string[] {
					"ComputeFrameworkEditor/Private",
				}
            );

            PublicDependencyModuleNames.AddRange(
				new string[]
				{
				}
	        );

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
					"ApplicationCore",
					"AssetTools",
					"ComputeFramework",
					"Core",
					"CoreUObject",
					"EditorFramework",
					"Engine",
					"MessageLog",
					"UnrealEd",
				}
			);
        }
    }
}
