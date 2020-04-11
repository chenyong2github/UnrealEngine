// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class MetalRHI : ModuleRules
{	
	public MetalRHI(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"ApplicationCore",
				"Engine",
				"RHI",
				"RenderCore",
			}
			);

		AddEngineThirdPartyPrivateStaticDependencies(Target,
			"MTLPP"
		);
			
		PublicWeakFrameworks.Add("Metal");

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.Add("QuartzCore");
		}

		string StatsPlugin = Path.Combine(EngineDirectory, "Restricted/NotForLicensees/Plugins/MetalStatistics/MetalStatistics.uplugin");
		bool bMetalStats = File.Exists(StatsPlugin);
		if ( bMetalStats && Target.Configuration != UnrealTargetConfiguration.Shipping )
		{
			PublicDefinitions.Add("METAL_STATISTICS=1");
		}
	}
}
