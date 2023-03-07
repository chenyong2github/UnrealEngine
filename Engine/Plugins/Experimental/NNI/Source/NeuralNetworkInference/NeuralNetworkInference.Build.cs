// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class NeuralNetworkInference : ModuleRules
{
	protected virtual bool bIsORTSupported { get => Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.Mac; }

	public NeuralNetworkInference( ReadOnlyTargetRules Target ) : base( Target )
	{
		ShortName = "NNI"; // Shorten to avoid path-too-long errors
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory, "..")
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"NeuralNetworkInferenceProfiling",
				"RenderCore",
				// Internal (Backend-related)
				"ModelProto"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				// Backend-related
				"ModelProtoFileReader",
				"NeuralNetworkInferenceShaders",
				"RHI",
				// ORT-related
				"Projects",
				"ThirdPartyHelperAndDLLLoader"
			}
		);

		if (bIsORTSupported)
		{
			// Define when UEAndORT-based NNI is available
			PublicDefinitions.Add("WITH_UE_AND_ORT_SUPPORT");

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"ONNXRuntime"				// Select this for open-source-based ONNX Runtime
					//"ONNXRuntimeDLL",			// Select this for DLL-based ONNX Runtime (Win64-only)
					//"ONNXRuntimeDLLHelper"	// Select this for DLL-based ONNX Runtime (Win64-only)
				});

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PrivateDependencyModuleNames.Add("D3D12RHI");

				AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "DirectML_1_8_0");

				if (Target.WindowsPlatform.bPixProfilingEnabled &&
					Target.Configuration != UnrealTargetConfiguration.Shipping &&
					Target.Configuration != UnrealTargetConfiguration.Test)
				{
					PrivateDependencyModuleNames.Add("WinPixEventRuntime");
				}
			}
		}

		// Win64-only
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDefinitions.Add("PLATFORM_WIN64");
		}
	}
}
