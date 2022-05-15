// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DataflowNodes : ModuleRules
	{
        public DataflowNodes(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("DataflowNodes/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Chaos",
					"DataflowCore"
				}
			);
		}
	}
}
