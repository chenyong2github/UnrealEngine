// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class InterchangeNodes : ModuleRules
	{
		public InterchangeNodes(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"InterchangeCore",
				}
			);

			if(Target.bCompileAgainstEngine)
			{
				PublicDependencyModuleNames.Add("Engine");
			}
		}
	}
}
