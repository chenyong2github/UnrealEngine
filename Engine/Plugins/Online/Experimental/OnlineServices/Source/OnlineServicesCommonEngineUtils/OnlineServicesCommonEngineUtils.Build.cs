// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OnlineServicesCommonEngineUtils : ModuleRules
{
	public OnlineServicesCommonEngineUtils(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		bUseUnity = false; // set false to catch non-unity compile issues while prototyping

		if (Target.bCompileAgainstEngine)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Engine"
				}
			);
		}

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreOnline",
				"CoreUObject",
				"OnlineServicesInterface",
				"OnlineServicesCommon",
				"OnlineBase",
				"Sockets"
			}
		);
	}
}
