// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class NNXProfiling : ModuleRules
{
	public NNXProfiling(ReadOnlyTargetRules Target) : base(Target)
	{

		ShortName = "NNXProfiling"; 
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory, "..")
			}
		);

		PublicDependencyModuleNames.AddRange
			(
			new string[] {
				"Core",
				"Engine",
			}
		);

		PrivateDependencyModuleNames.AddRange
			(
			new string[] {	}
		);

		PublicDefinitions.Add("WITH_NNI_STATS");
	}
}
