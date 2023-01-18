// Copyright Epic Games, Inc. All Rights Reserved.


using UnrealBuildTool;
using System.IO;

public class NNEUtils : ModuleRules
{
	public NNEUtils(ReadOnlyTargetRules Target) : base(Target)
	{
		ShortName = "NNEUtils"; // Shorten to avoid path-too-long errors
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				Path.GetFullPath(Path.Combine(EngineDirectory, "Plugins/Experimental/NNE/Source/NNXCore/Private")),
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"NNXCore"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"NNX_ONNXRuntime",
				"NNX_ONNX_1_11_0",
				"NNX_ONNXRuntimeProto_1_11_0",
				"ORTHelper",
				}
			);


		if (Target.Platform == UnrealTargetPlatform.Win64 || 
			Target.Platform == UnrealTargetPlatform.Linux ||
			Target.Platform == UnrealTargetPlatform.Mac)
		{
			PrivateDependencyModuleNames.AddRange
				(
				new string[] {
					"Protobuf",
					"Re2" // ONNXRuntimeRE2
				}
			);
		}
	}
}

