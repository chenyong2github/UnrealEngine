// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RivermaxCore : ModuleRules
{
	public RivermaxCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"MediaAssets",
				"MediaIOCore",
				"RivermaxLib",
				"TimeManagement"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Engine",
				"RenderCore",
				"RHI"
			}
		);

		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
				}
			);
		}
	}
}
