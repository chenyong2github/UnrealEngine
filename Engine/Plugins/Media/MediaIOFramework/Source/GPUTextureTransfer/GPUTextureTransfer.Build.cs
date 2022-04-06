// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GPUTextureTransfer : ModuleRules
	{
		public GPUTextureTransfer(ReadOnlyTargetRules Target) : base(Target)
		{
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "GPUDirect");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
			}
				
			PublicDefinitions.Add("PERF_LOGGING=0");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"RHI"
				});

			PublicIncludePaths.AddRange(
					new string[]
				{
				});

			PrivateIncludePaths.AddRange(
				new string[] {
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"GPUDirect",
					"RenderCore",
					"RHI",
					"VulkanRHI",
				});

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("EditorFramework");
				PrivateDependencyModuleNames.Add("UnrealEd");
			}
		}

	}
}
