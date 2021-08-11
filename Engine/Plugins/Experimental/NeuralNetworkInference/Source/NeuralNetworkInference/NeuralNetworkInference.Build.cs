// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NeuralNetworkInference : ModuleRules
{
	public NeuralNetworkInference( ReadOnlyTargetRules Target ) : base( Target )
	{
        ShortName = "DeprecatedNNIWE"; // Could be removed when plugin moves to Experimental, NFL path is too long
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
				"NeuralNetworkInferenceBackEnd",
				"NeuralNetworkInferenceCore"
			}
		);

		PrivateDependencyModuleNames.AddRange
			(
			new string[] {
                "ONNXRuntime",				// Select this for open-source-based ONNX Runtime
				//"ONNXRuntimeDLL",			// Select this for DLL-based ONNX Runtime
				//"ONNXRuntimeDLLHelper",	// Select this for DLL-based ONNX Runtime
				"Projects",
				"ThirdPartyHelperAndDLLLoader"
			}
		);

		// Win64-only
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDefinitions.Add("PLATFORM_WIN64");
		}
	}
}
