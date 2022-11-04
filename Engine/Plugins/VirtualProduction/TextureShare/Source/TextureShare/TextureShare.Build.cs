// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

[SupportedPlatforms("Win64")]
public class TextureShare : ModuleRules
{
	public TextureShare(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				Path.Combine(EngineDirectory,"Source/Runtime/Renderer/Private"),
				Path.Combine(EngineDirectory,"Plugins/VirtualProduction/TextureShare/Source/TextureShareCore/Private"),
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"RHI",
				"Renderer",
				"RenderCore",
				"Slate",
				"SlateCore",
				"TextureShareCore"
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"D3D11RHI",
					"D3D12RHI"
				}
			);
		}

		// Allow using Vulkan render device
		bool bSupportDeviceVulkan = false;

		if (bSupportDeviceVulkan)
		{
			PublicDefinitions.Add("TEXTURESHARE_VULKAN=1");

			// Support Vulkan device
			PrivateDependencyModuleNames.AddRange(new string[] { "VulkanRHI" });
			PrivateIncludePathModuleNames.Add("VulkanRHI");

			AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
		}
		else
		{
			PublicDefinitions.Add("TEXTURESHARE_VULKAN=0");
		}

		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelExtensionsFramework");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
