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

		PublicIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory, "../NNXRuntimeORT/Public/")
			}
		);

		PublicDependencyModuleNames.AddRange
			(
			new string[] {
				"Core",
				"Engine",
				"NNXCore",
				"NNXProfiling",
				"ORTHelper",
			}
		);

		PrivateDependencyModuleNames.AddRange
			(
			new string[] {
				//"ORTDefault",
				"NNX_ONNXRuntime"	// Select this for open-source-based ONNX Runtime
			}
		);
	}
}
