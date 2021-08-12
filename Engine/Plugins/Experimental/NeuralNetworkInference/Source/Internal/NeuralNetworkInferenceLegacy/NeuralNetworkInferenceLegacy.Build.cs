// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NeuralNetworkInferenceLegacy : ModuleRules
{
	public NeuralNetworkInferenceLegacy( ReadOnlyTargetRules Target ) : base( Target )
	{
        ShortName = "NNILegacy"; // Could be removed when plugin moves to Experimental, NFL path is too long
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
				"ModelProto",
				"NeuralNetworkInference",
				"NeuralNetworkInferenceCore"
			}
		);

		PrivateDependencyModuleNames.AddRange
			(
			new string[] {
				"NeuralNetworkInferenceShaders",
				"Projects",
				"RenderCore",
				"RHI"
			}
		);

		// Editor-only
		if (Target.bBuildEditor)
		{
			// Win64-only
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PrivateDependencyModuleNames.AddRange
					(
					new string[] {
						"ModelProtoFileReader"
					}
				);
			}
		}
	}
}
