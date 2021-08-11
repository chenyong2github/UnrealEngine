// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NeuralNetworkInferenceQAEditor : ModuleRules
{
	public NeuralNetworkInferenceQAEditor( ReadOnlyTargetRules Target ) : base( Target )
	{
        ShortName = "NNIQAEditor"; // Could be removed when plugin moves to Experimental, NFL path is too long
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
				"Engine"
			}
		);

		PrivateDependencyModuleNames.AddRange
			(
			new string[] {
				"NeuralNetworkInferenceEditor",
				"NeuralNetworkInferenceQA",
				"UnrealEd"
			}
		);
	}
}
