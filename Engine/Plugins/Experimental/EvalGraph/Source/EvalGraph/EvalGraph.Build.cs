// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class EvalGraph : ModuleRules
	{
        public EvalGraph(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("EvalGraph/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Chaos",
				}
				);
		}
	}
}
