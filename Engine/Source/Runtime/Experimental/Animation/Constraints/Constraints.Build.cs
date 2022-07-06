// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Constraints : ModuleRules
{
	public Constraints(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateIncludePaths.Add("Runtime/Experimental/Animation/Constraints/Private");
		PublicIncludePaths.Add(ModuleDirectory + "/Public");

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AnimationCore",
				"MovieScene"
			}
		);
	}
}