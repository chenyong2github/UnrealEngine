// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ChaosEngineBridge : ModuleRules
	{
        public ChaosEngineBridge(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"ChaosSolvers",
					"Chaos",
					"PhysicsCore"
                }
				);
		}
	}
}
