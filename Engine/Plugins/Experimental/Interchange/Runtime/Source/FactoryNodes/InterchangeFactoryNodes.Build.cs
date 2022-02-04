// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class InterchangeFactoryNodes : ModuleRules
	{
		public InterchangeFactoryNodes(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"InterchangeCore",
					"InterchangeNodes",
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"CinematicCamera",
					"Engine"
				}
			);
		}
	}
}
