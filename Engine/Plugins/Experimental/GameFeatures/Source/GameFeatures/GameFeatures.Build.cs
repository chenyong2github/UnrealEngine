// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GameFeatures : ModuleRules
	{
        public GameFeatures(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
                {
                    "Core",
                    "CoreUObject",
					"DeveloperSettings",
					"Engine",
                    "ModularGameplay"
                }
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"GameplayTags",
					"InstallBundleManager",
					"Json",
					"PakFile",
					"Projects"
				}
			);
		}
	}
}
