// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AnimationWarpingRuntime : ModuleRules
	{
		public AnimationWarpingRuntime(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
			new string[]
			{
                "AnimGraphRuntime",
                "AnimationCore",
                "Core",
                "CoreUObject",
                "Engine",
            });

			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
                "SlateCore",
            });
		}
	}
}
