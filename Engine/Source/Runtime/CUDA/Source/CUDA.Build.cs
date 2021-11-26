// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;
using System.Globalization;

public class CUDA : ModuleRules
{
	public CUDA(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"RenderCore",
			"RHI",
			"Engine",
		});
		
		PublicDependencyModuleNames.Add("CUDAHeader");

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows) || Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicDefinitions.Add("PLATFORM_SUPPORTS_CUDA=1");
			var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

			AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
			
			PrivateIncludePathModuleNames.Add("VulkanRHI");
			PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private"));	

			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
			{
				PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private/Linux"));	
			} else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
			{
				PublicSystemLibraries.AddRange(new string[] {
					"DXGI.lib",
					"d3d11.lib",
					"d3d12.lib"
				});

				PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private/Windows"));
			}

			AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
		}
		else
		{
			PublicDefinitions.Add("PLATFORM_SUPPORTS_CUDA=0");
		}
	}
}
