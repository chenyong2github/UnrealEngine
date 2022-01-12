// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OnlineServicesCommon : ModuleRules
{
	public OnlineServicesCommon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		bUseUnity = false; // set false to catch non-unity compile issues while prototyping

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"CoreOnline",
				"OnlineServicesInterface"
			}
		);

		PublicIncludePaths.Add(ModuleDirectory);

		// OnlineService cannot depend on Engine!
		PrivateDependencyModuleNames.AddRange(
			new string[] { 
				"Core",
				"CoreUObject"		// CoreUObject temporary dependency
			}
		);
	}
}
