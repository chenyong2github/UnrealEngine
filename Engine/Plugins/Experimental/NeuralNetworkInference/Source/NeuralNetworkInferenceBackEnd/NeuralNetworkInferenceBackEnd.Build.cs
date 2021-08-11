// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NeuralNetworkInferenceBackEnd : ModuleRules
{
	public NeuralNetworkInferenceBackEnd( ReadOnlyTargetRules Target ) : base( Target )
	{
        ShortName = "NNIBackEnd"; // Could be removed when plugin moves to Experimental, NFL path is too long
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
				"ModelProto",
				"NeuralNetworkInferenceShaders",
				"RenderCore",
				"RHI"
			}
		);
	}
}
