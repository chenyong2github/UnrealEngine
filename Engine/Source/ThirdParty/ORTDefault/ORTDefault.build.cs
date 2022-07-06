// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class ORTDefault : ModuleRules
{
    public ORTDefault(ReadOnlyTargetRules Target) : base(Target)
    {
		Type = ModuleType.External;
		// Win64
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// PublicSystemIncludePaths
			// string IncPath = Path.Combine(ModuleDirectory, "include/");
			// PublicSystemIncludePaths.Add(IncPath);

			// PublicSystemIncludePaths
			PublicIncludePaths.AddRange(
				new string[] {
					System.IO.Path.Combine(ModuleDirectory, "include/"),
					System.IO.Path.Combine(ModuleDirectory, "include/onnxruntime"),
					System.IO.Path.Combine(ModuleDirectory, "include/onnxruntime/core/session")
				}
			);

			// PublicAdditionalLibraries
			string PlatformDir = Target.Platform.ToString();
			string LibDirPath = Path.Combine(ModuleDirectory, "lib", PlatformDir);
			string[] LibFileNames = new string[] {
				"onnxruntime",
				"onnxruntime_providers_cuda",
				"onnxruntime_providers_shared",
				"custom_op_library",
				"test_execution_provider"
			};


			string BinaryThirdPartyDirPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "bin", PlatformDir));
			foreach (string LibFileName in LibFileNames)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibDirPath, LibFileName + ".lib"));
				// PublicDelayLoadDLLs
				string DLLFileName = LibFileName + ".dll";
				PublicDelayLoadDLLs.Add(DLLFileName);
				// RuntimeDependencies
				string DLLFullPath = Path.Combine(BinaryThirdPartyDirPath, DLLFileName);
				RuntimeDependencies.Add(DLLFullPath);
			}

			// PublicDefinitions
			PublicDefinitions.Add("ONNXRUNTIME_USE_DLLS");
			PublicDefinitions.Add("WITH_ONNXRUNTIME");
			PublicDefinitions.Add("ONNXRUNTIME_PLATFORM_PATH=bin/" + PlatformDir);
			PublicDefinitions.Add("ONNXRUNTIME_DLL_NAME=" + "onnxruntime.dll");
			PublicDefinitions.Add("WIN32_LEAN_AND_MEAN");
		}
	}
}
