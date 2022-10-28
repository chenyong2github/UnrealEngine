// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DataflowEnginePlugin : ModuleRules
	{
        public DataflowEnginePlugin(ReadOnlyTargetRules Target) : base(Target)
		{
			bTreatAsEngineModule = true;
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"DataflowCore",
					"DataflowEngine",
					"Chaos",
					"ProceduralMeshComponent"
				}
			);
		}
	}
}
