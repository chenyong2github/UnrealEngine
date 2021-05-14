// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterProjection : ModuleRules
{
	public DisplayClusterProjection(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"DisplayClusterConfiguration",
				"DisplayClusterShaders",
			});

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"DisplayCluster",
				"Engine",
				"Projects"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Composure",
				"DisplayCluster",
				"Projects",
				"RenderCore",
				"RHI"
			});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("ProceduralMeshComponent");
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"D3D11RHI",
					"D3D12RHI"
			});

			// Required for some private headers needed for the rendering support.
			var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
			PrivateIncludePaths.AddRange(
				new string[] {
					Path.Combine(EngineDir, @"Source\Runtime\Windows\D3D11RHI\Private"),
					Path.Combine(EngineDir, @"Source\Runtime\Windows\D3D11RHI\Private\Windows"),
					Path.Combine(EngineDir, @"Source\Runtime\D3D12RHI\Private"),
					Path.Combine(EngineDir, @"Source\Runtime\D3D12RHI\Private\Windows")
			});

			AddEngineThirdPartyPrivateStaticDependencies(Target, "AMD_AGS");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelExtensionsFramework");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
		}

		AddThirdPartyDependencies(ROTargetRules);
	}

	public void AddThirdPartyDependencies(ReadOnlyTargetRules ROTargetRules)
	{
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// EasyBlend
			PrivateIncludePaths.Add(Path.Combine(ThirdPartyPath, "EasyBlend", "Include"));
			RuntimeDependencies.Add(Path.Combine(ThirdPartyPath, "EasyBlend", "DLL", "mplEasyBlendSDKDX1164.dll"));

			// VIOSO
			PrivateIncludePaths.Add(Path.Combine(ThirdPartyPath, "VIOSO", "Include"));
			RuntimeDependencies.Add(Path.Combine(ThirdPartyPath, "VIOSO", "DLL", "VIOSOWarpBlend64.dll"));

			// Domeprojection
			PrivateIncludePaths.Add(Path.Combine(ThirdPartyPath, "Domeprojection", "Include"));
			RuntimeDependencies.Add(Path.Combine(ThirdPartyPath, "Domeprojection", "DLL", "dpLib.dll"));
			RuntimeDependencies.Add(Path.Combine(ThirdPartyPath, "Domeprojection", "DLL", "WibuCm64.dll"));
		}
	}
}
