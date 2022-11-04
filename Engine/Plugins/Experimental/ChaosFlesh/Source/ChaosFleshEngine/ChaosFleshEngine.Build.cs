// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ChaosFleshEngine : ModuleRules
	{
        public ChaosFleshEngine(ReadOnlyTargetRules Target) : base(Target)
		{
			SetupModulePhysicsSupport(Target);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Chaos",
					"Engine",
                    "RenderCore",
                    "RHI",
					"Renderer",
                    "FieldSystemEngine",
	                "ChaosFlesh",
					"NetCore",
					"ProceduralMeshComponent",
					"DataflowCore",
					"DataflowEngine",
					"ChaosCaching",
				}
				);


			PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
		}
	}
}
