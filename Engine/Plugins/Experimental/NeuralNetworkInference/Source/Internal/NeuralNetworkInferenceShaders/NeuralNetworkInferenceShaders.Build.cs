// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NeuralNetworkInferenceShaders : ModuleRules
{
	public NeuralNetworkInferenceShaders( ReadOnlyTargetRules Target ) : base( Target )
	{
        ShortName = "NNIShaders"; // Could be removed when plugin moves to Experimental, NFL path is too long
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
				"CoreUObject",
				"Engine",
				"NeuralNetworkInferenceCore"
			}
		);

		PrivateDependencyModuleNames.AddRange
			(
			new string[] {
				"Projects",
				"RenderCore",
				"RHI"
			}
		);
	}
}
