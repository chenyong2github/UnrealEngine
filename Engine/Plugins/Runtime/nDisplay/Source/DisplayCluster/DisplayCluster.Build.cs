// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayCluster : ModuleRules
{
	public DisplayCluster(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(
			new string[] {
				"DisplayCluster/Private",
				"../../../../../Engine/Source/Runtime/Renderer/Private",
				"../../../../../Engine/Source/Runtime/Windows/D3D11RHI/Private",
				"../../../../../Engine/Source/Runtime/Windows/D3D11RHI/Private/Windows",
				"../../../../../Engine/Source/Runtime/D3D12RHI/Private",
				"../../../../../Engine/Source/Runtime/D3D12RHI/Private/Windows"
			});

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Json",
				"JsonUtilities"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"D3D11RHI",
				"D3D12RHI",
				"Engine",
				"HeadMountedDisplay",
				"InputCore",
				"Networking",
				"OpenGLDrv",
				"RHI",
				"RenderCore",
				"Slate",
				"SlateCore",
				"Sockets"
			});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		PublicAdditionalLibraries.Add("opengl32.lib");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenGL");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");

		// 3rd party dependencies
		AddThirdPartyDependencies(ROTargetRules);
	}


	public bool AddThirdPartyDependencies(ReadOnlyTargetRules ROTargetRules)
	{
		string ModulePath = Path.GetDirectoryName(UnrealBuildTool.RulesCompiler.GetFileNameFromType(GetType()));
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModulePath, "../../ThirdParty/"));

		string PathLib = string.Empty;
		string PathInc = string.Empty;

		// VRPN
		PathLib = Path.Combine(ThirdPartyPath, "VRPN/Lib");
		PathInc = Path.Combine(ThirdPartyPath, "VRPN/Include");
		PublicAdditionalLibraries.Add(Path.Combine(PathLib, "vrpn.lib"));
		PublicAdditionalLibraries.Add(Path.Combine(PathLib, "quat.lib"));
		PublicIncludePaths.Add(PathInc);

		return true;
	}
}
