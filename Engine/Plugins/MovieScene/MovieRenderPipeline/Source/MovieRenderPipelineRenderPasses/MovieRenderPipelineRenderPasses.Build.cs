// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MovieRenderPipelineRenderPasses : ModuleRules
{
	public MovieRenderPipelineRenderPasses(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"ImageWriteQueue",
                "SignalProcessing", // Needed for wave writer.
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"MovieRenderPipelineCore",
				"RenderCore",
                "RHI",
            }
        );
	}
}
