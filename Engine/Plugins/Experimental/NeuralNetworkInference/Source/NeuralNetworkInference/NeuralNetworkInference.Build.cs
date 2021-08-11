// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NeuralNetworkInference : ModuleRules
{
	public NeuralNetworkInference( ReadOnlyTargetRules Target ) : base( Target )
	{
		// Define when ORT-based NNI is available
		bool bIsORTSupported = (Target.Platform == UnrealTargetPlatform.Win64 /*|| Target.Platform == UnrealTargetPlatform.Linux*/);
		if (bIsORTSupported)
		{
			PublicDefinitions.Add("WITH_FULL_NNI_SUPPORT");
		}

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
				"Projects",
				"ThirdPartyHelperAndDLLLoader"
			}
		);

		if (bIsORTSupported)
		{

			PrivateDependencyModuleNames.AddRange
				(
				new string[] {
				"ONNXRuntime"				// Select this for open-source-based ONNX Runtime
				//"ONNXRuntimeDLL",			// Select this for DLL-based ONNX Runtime (Win64-only)
				//"ONNXRuntimeDLLHelper"	// Select this for DLL-based ONNX Runtime (Win64-only)
				}
			);
		}

		// Win64-only
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDefinitions.Add("PLATFORM_WIN64");
		}
	}
}
