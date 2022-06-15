// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class WaveTable : ModuleRules
	{
		public WaveTable(ReadOnlyTargetRules Target) : base(Target)
		{
			OptimizeCode = CodeOptimization.Never;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CoreUObject",
					"SignalProcessing"
				}
			);
		}
	}
}