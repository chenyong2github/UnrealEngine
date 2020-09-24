// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class TextureShareD3D12 : ModuleRules
{
	public TextureShareD3D12(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
						new string[]
						{
							"Core",
							"Engine",
							"RenderCore",
							"RHI",
							"D3D12RHI",
						});

		PrivateIncludePaths.AddRange(
			new string[]
			{
						"../../../../Source/Runtime/D3D12RHI/Private",
						"../../../../Source/Runtime/D3D12RHI/Private/Windows",
						"../../../../Source/ThirdParty/Windows/D3DX12/Include"
			});

		// Allow D3D12 Cross GPU Heap resource API (experimental)
		PublicDefinitions.Add("TEXTURESHARE_CROSSGPUHEAP=0");

		///////////////////////////////////////////////////////////////
		// Platform specific defines
		///////////////////////////////////////////////////////////////
		if (!Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && Target.Platform != UnrealTargetPlatform.XboxOne)
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) ||
			Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
			if (Target.Platform != UnrealTargetPlatform.HoloLens)
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "AMD_AGS");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
			}
		}
	}
}
