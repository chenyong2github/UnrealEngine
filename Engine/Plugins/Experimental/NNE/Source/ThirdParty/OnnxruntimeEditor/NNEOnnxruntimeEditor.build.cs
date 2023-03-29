// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class NNEOnnxruntimeEditor : ModuleRules
{
    public NNEOnnxruntimeEditor(ReadOnlyTargetRules Target) : base(Target)
    {
		Type = ModuleType.External;
		// Win64
		if (Target.Platform == UnrealTargetPlatform.Win64 || 
			Target.Platform == UnrealTargetPlatform.Mac ||
			Target.Platform == UnrealTargetPlatform.Linux)
		{

			// PublicSystemIncludePaths
			PublicSystemIncludePaths.AddRange(
				new string[] {
					System.IO.Path.Combine(ModuleDirectory, "include/"),
					System.IO.Path.Combine(ModuleDirectory, "include/onnxruntime"),
					System.IO.Path.Combine(ModuleDirectory, "include/onnxruntime/core/session")
				}
			);

			// PublicAdditionalLibraries
			string[] LibFileNames;
			if(Target.Platform == UnrealTargetPlatform.Win64)
			{
				LibFileNames = new string[] {
					"onnxruntime",
					//"onnxruntime_providers_cuda",
					//"onnxruntime_providers_shared",
					//"custom_op_library",
					//"test_execution_provider"
				};
			}
			else if(Target.Platform == UnrealTargetPlatform.Linux)
			{
				LibFileNames = new string[] {
					"onnxruntime",
					//"onnxruntime_providers_shared",
					//"custom_op_library",
					//"test_execution_provider"
				};
			}
			else if(Target.Platform == UnrealTargetPlatform.Mac)
			{
				LibFileNames = new string[] {
					"onnxruntime",
					//"custom_op_library"
				};
			}
			else 
			{
				LibFileNames = new string[] {};
			}

			string PlatformDir = Target.Platform.ToString();
			string OrtPlatformRelativePath = Path.Combine("Binaries", "ThirdParty", "OnnxruntimeEditor", PlatformDir);
			string OrtPlatformPath = Path.Combine(PluginDirectory, OrtPlatformRelativePath);
			
			foreach (string LibFileName in LibFileNames)
			{
				if(Target.Platform == UnrealTargetPlatform.Win64)
				{
					PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib", PlatformDir, LibFileName + ".lib"));
					
					// PublicDelayLoadDLLs
					string DLLFileName = LibFileName + ".dll";
					PublicDelayLoadDLLs.Add(DLLFileName);

					// RuntimeDependencies
					string DLLFullPath = Path.Combine(OrtPlatformPath, DLLFileName);
					RuntimeDependencies.Add(DLLFullPath);
				} 
				else if(Target.Platform == UnrealTargetPlatform.Linux)
				{
					string CurrentLibPath = Path.Combine(OrtPlatformPath, "lib" + LibFileName + ".so");			
					PublicAdditionalLibraries.Add(CurrentLibPath);
					PublicDelayLoadDLLs.Add(CurrentLibPath);
					RuntimeDependencies.Add(CurrentLibPath);

				}
				else if(Target.Platform == UnrealTargetPlatform.Mac)
				{
					string CurrentLibPath = Path.Combine(OrtPlatformPath, "lib" + LibFileName + ".dylib");
					PublicDelayLoadDLLs.Add(CurrentLibPath);
					RuntimeDependencies.Add(CurrentLibPath);
				}
			}

			// PublicDefinitions
			PublicDefinitions.Add("ONNXRUNTIME_USE_DLLS");
			PublicDefinitions.Add("WITH_ONNXRUNTIME");

			if (!Target.bBuildEditor)
			{
				PublicDefinitions.Add("ORT_NO_EXCEPTIONS");
			}
			
			PublicDefinitions.Add("ONNXRUNTIME_PLATFORM_PATH=" + OrtPlatformRelativePath.Replace('\\', '/'));
			
			if(Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicDefinitions.Add("ONNXRUNTIME_DLL_NAME=" + "onnxruntime.dll");
			}
			else if(Target.Platform == UnrealTargetPlatform.Linux)
			{
				PublicDefinitions.Add("ONNXRUNTIME_DLL_NAME=" + "libonnxruntime.so");
			}
			else if(Target.Platform == UnrealTargetPlatform.Mac)
			{
				PublicDefinitions.Add("ONNXRUNTIME_DLL_NAME=" + "libonnxruntime.dylib");
			}

			PublicDefinitions.Add("ORT_API_MANUAL_INIT");
		}
	}
}
