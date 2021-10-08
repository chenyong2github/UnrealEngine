// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class ONNXRuntimeProto_1_10_1 : ModuleRules
{
	public ONNXRuntimeProto_1_10_1(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		// Win64 or Linux
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// PublicSystemIncludePaths
			string IncPath = Path.Combine(ModuleDirectory, "include");
			PublicSystemIncludePaths.Add(IncPath);
			// PublicAdditionalLibraries
			string PlatformDir = Target.Platform.ToString();
			string LibDirPath = Path.Combine(ModuleDirectory, "lib", PlatformDir);
			string[] LibFileNames = new string[] {
				"onnx_proto",
			};
			
			foreach (string LibFileName in LibFileNames)
			{
				if(Target.Platform == UnrealTargetPlatform.Win64)
				{
					PublicAdditionalLibraries.Add(Path.Combine(LibDirPath, LibFileName + ".lib"));
				}
			}

			// PublicDefinitions
			PublicDefinitions.Add("WITH_ONNXRUNTIME_PROTO");
			PublicDefinitions.Add("ONNX_NO_EXCEPTIONS");
		}
	}
}
