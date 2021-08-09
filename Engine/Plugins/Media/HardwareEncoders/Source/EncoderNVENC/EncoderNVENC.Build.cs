// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System;

public class EncoderNVENC: ModuleRules
{
	public EncoderNVENC(ReadOnlyTargetRules Target) : base(Target)
	{
		// Without these two compilation fails on VS2017 with D8049: command line is too long to fit in debug record.
		bLegacyPublicIncludePaths = false;
		DefaultBuildSettings = BuildSettingsVersion.V2;

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Engine",
			"AVEncoder",
			"VulkanRHI",
			"nvEncode",
			"CUDA"
		});

		PublicDependencyModuleNames.AddRange(new string[] {
			"RenderCore",
			"Core",
			"RHI"
		});

		string EngineSourceDirectory = Path.GetFullPath(Target.RelativeEnginePath);

		PrivateIncludePaths.Add(Path.Combine(EngineSourceDirectory, "Source/Runtime/AVEncoder/Private"));

		AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
		
		PrivateIncludePaths.Add(Path.Combine(EngineSourceDirectory, "Source/Runtime/VulkanRHI/Private"));
		AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			PublicDependencyModuleNames.Add("D3D11RHI");
			PublicDependencyModuleNames.Add("D3D12RHI");

			PublicSystemLibraries.AddRange(new string[] {
				"DXGI.lib",
				"d3d11.lib",
				"d3d12.lib"
			});

			PrivateIncludePaths.Add(Path.Combine(EngineSourceDirectory, "Source/Runtime/VulkanRHI/Private/Windows"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PrivateIncludePaths.Add(Path.Combine(EngineSourceDirectory, "Source/Runtime/VulkanRHI/Private/Linux"));
		}
	}
}
