// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class Reflex : ModuleRules
	{
		public Reflex(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Engine",
					"RHI",
					"CoreUObject"
				}
			);

			// Grab NVAPI
			AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
		}
	}
}
