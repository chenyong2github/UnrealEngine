// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NNXQA : ModuleRules
{
	public NNXQA(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"Projects",
				"Json",
				"CoreUObject",
				"NNXCore",
				"NNXUtils"
			}
		);
	}
}
