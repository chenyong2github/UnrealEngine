// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NNIHlslShaders : ModuleRules
{
	public NNIHlslShaders( ReadOnlyTargetRules Target ) : base( Target )
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

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
