// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;
using System.Globalization;
using EpicGames.Core;

public class CUDA : ModuleRules
{
	public CUDA(ReadOnlyTargetRules Target) : base(Target)
	{

		// This module is pending TPS approval so for now we make it do nothing.
		PrivateDependencyModuleNames.AddRange(new string[] {"Core"});
		PublicDefinitions.Add("WITH_CUDA=0");
		return;

		// // Early exit on Windows and set WITH_CUDA to 0 so it CUDA module source does nothing.
		// // Todo: Add CUDA support for Windows. 
		// // Note: Turning this on will change some code paths in Windows Pixel Streaming and require some fixes.
		// if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
		// {
		// 	PrivateDependencyModuleNames.AddRange(new string[] {"Core"});
		// 	PublicDefinitions.Add("WITH_CUDA=0");
		// 	return;
		// }

		// var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

		// PrivateDependencyModuleNames.AddRange(new string[] {
		// 	"Core",
		// 	"RenderCore",
		// 	"RHI",
		// 	"Engine",
		// 	"VulkanRHI"
		// });

		// PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private"));
		// PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private/Linux"));
		
		// AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");

		// // TODO (M84FIX) this could probably be done better	
		// bool bCudaDirExists = (Environment.GetEnvironmentVariable("CUDA_PATH") != null) || Directory.Exists("/usr/local/cuda");

		// if(bCudaDirExists)
		// {
		// 	//if(Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
		// 	//{
		// 		// TODO (M84FIX) this could probably be done better
		// 		// string CudaPath = Environment.GetEnvironmentVariable("CUDA_PATH");
		// 		// string ArchPath = (Target.Platform == UnrealTargetPlatform.Win64) ? "x64" : "Win32";
		// 		// PublicIncludePaths.Add(Path.Combine(CudaPath, "include"));
		// 		// PublicSystemLibraryPaths.Add(Path.Combine(CudaPath, "lib", ArchPath));
		// 		// PublicSystemLibraries.Add("cuda.lib");
		// 	//}
		// 	//else 
		// 	if (Target.Platform == UnrealTargetPlatform.Linux)
		// 	{      
		// 		PublicIncludePaths.Add("/usr/local/cuda/include");
		// 		PublicSystemLibraryPaths.Add("/lib/x86_64-linux-gnu");
		// 		PublicSystemLibraries.Add("cuda");
				
		// 		PublicDefinitions.Add("WITH_CUDA=1");
		// 	}
		// }
		// else
		// {
		// 	if (Target.Platform == UnrealTargetPlatform.Linux)
		// 	{
		// 		Log.TraceError("NVIDIA CUDA Toolkit not detected, building with CUDA failed!");
		// 		throw new FileNotFoundException("NVIDIA CUDA Toolkit not detected, please install the NVIDIA CUDA Toolkit.");
		// 	}

		// 	PublicDefinitions.Add("WITH_CUDA=0");
		// }
	}
}
