// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class EvalGraphEngine : ModuleRules
	{
        public EvalGraphEngine(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("EvalGraphEngine/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"SlateCore",
					"EvalGraph"
				}
				);
		}
	}
}
