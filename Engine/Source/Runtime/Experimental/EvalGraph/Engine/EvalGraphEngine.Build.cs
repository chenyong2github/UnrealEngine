// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class EvalGraphEngine : ModuleRules
	{
        public EvalGraphEngine(ReadOnlyTargetRules Target) : base(Target)
		{
			bTreatAsEngineModule = true;
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"EvalGraphCore"
				}
				);
		}
	}
}
