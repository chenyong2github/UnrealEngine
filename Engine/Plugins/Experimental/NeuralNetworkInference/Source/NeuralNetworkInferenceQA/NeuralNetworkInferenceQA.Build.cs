// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NeuralNetworkInferenceQA : ModuleRules
{
	public NeuralNetworkInferenceQA( ReadOnlyTargetRules Target ) : base( Target )
	{
        ShortName = "NNIQA"; // Could be removed when plugin moves to Experimental, NFL path is too long
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
				"NeuralNetworkInference"
			}
		);

		PrivateDependencyModuleNames.AddRange
			(
			new string[] {
				"ModelProto",
				"ModelProtoFileReader",
				"NeuralNetworkInferenceCore"
			}
		);
	}
}
