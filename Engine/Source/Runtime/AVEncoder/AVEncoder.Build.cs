// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System;

public class AVEncoder : ModuleRules
{
	public AVEncoder(ReadOnlyTargetRules Target) : base(Target)
	{
        // Without these two compilation fails on VS2017 with D8049: command line is too long to fit in debug record.
        bLegacyPublicIncludePaths = false;
        DefaultBuildSettings = BuildSettingsVersion.V2;

        // PCHUsage = PCHUsageMode.NoPCHs;

        // PrecompileForTargets = PrecompileTargetsType.None;

        PublicIncludePaths.AddRange(new string[] {
			// ... add public include paths required here ...
		});

		PrivateIncludePaths.AddRange(new string[] {
			// ... add other private include paths required here ...
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Engine",
			"nvEncode"
			// "Amf" // TODO waiting on cross platform Amf encoder
		});

		PublicDependencyModuleNames.AddRange(new string[] {
			"RenderCore",
			"Core",
			"RHI"
			// ... add other public dependencies that you statically link with here ...
		});

		DynamicallyLoadedModuleNames.AddRange(new string[] {
			// ... add any modules that your module loads dynamically here ...
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// d3d to be able to use NVENC
			PublicSystemLibraries.AddRange(new string[] {
				"dxgi.lib",
				"d3d11.lib",
				"d3d12.lib",
				"mfplat.lib",
				"mfuuid.lib"
			});
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PrivateDependencyModuleNames.Add("CUDA");			
		}
		
		// TEMPORARY: set this to zero for all platforms until CUDA TPS review clears
		PublicDefinitions.Add("WITH_CUDA=0");
	}
}
