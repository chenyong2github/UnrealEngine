// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NNXHlslShaders : ModuleRules
{
	public NNXHlslShaders( ReadOnlyTargetRules Target ) : base( Target )
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		//PublicIncludePaths.AddRange(
		//	new string[] {
		//		System.IO.Path.Combine(ModuleDirectory, "..")
		//	}
		//);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Projects",
				"RenderCore",
				"RHI",
				"NNXCore"
			}
		);
	}
}
