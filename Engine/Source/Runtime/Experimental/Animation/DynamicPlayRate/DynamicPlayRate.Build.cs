// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DynamicPlayRate : ModuleRules
{
	public DynamicPlayRate(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateIncludePaths.Add("Runtime/Experimental/Animation/DynamicPlayRate/Private");
		PublicIncludePaths.Add(ModuleDirectory + "/Public");

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
                "Engine",
				"AnimGraphRuntime"
			}
		);
	}
}
