// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PUMARuntime : ModuleRules
	{
		public PUMARuntime(ReadOnlyTargetRules Target) : base(Target)
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
