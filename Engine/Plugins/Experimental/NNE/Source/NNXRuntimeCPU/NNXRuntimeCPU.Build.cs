// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class NNXRuntimeCPU : ModuleRules
{
	public NNXRuntimeCPU( ReadOnlyTargetRules Target ) : base( Target )
	{
		// Define when UEAndORT-based NNI is available
		bool bIsORTSupported = (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.Mac);
		
		ShortName = "NNXRtCpu"; // Shorten to avoid path-too-long errors
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange
			(
			new string[] {
				"Core",
				"Engine",
				"NNXCore",
				"NNEProfiling",
				"ORTHelper",
			}
		);

		PrivateDependencyModuleNames.AddRange
			(
			new string[] {
				//"ORTDefault",
				"CoreUObject",
				"NNEUtils",
				"NNX_ONNXRuntime"	// Select this for open-source-based ONNX Runtime
			}
		);
	}
}
