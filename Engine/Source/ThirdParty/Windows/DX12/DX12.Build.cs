// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class DX12 : ModuleRules
{
	public DX12(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string DirectXSDKDir = "";
		if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			DirectXSDKDir = Target.WindowsPlatform.bUseWindowsSDK10 ?
			Target.UEThirdPartySourceDirectory + "Windows/DirectXLegacy" :
			Target.UEThirdPartySourceDirectory + "Windows/DirectX";
		}
		else
		{
			DirectXSDKDir = Target.UEThirdPartySourceDirectory + "Windows/DirectX";
		}

		string LibDir = null;
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			LibDir = DirectXSDKDir + "/Lib/x64/";
        }
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PublicSystemLibraries.Add("dxgi.lib"); // For DXGIGetDebugInterface1

			bool PixAvalable = (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64);
			if (PixAvalable &&
				//Target.WindowsPlatform.bUseWindowsSDK10 &&
				Target.WindowsPlatform.bPixProfilingEnabled &&
				Target.Configuration != UnrealTargetConfiguration.Shipping &&
				Target.Configuration != UnrealTargetConfiguration.Test)
			{
				string Arch = Target.WindowsPlatform.GetArchitectureSubpath();
				PublicSystemIncludePaths.Add(Target.UEThirdPartySourceDirectory + "/Windows/Pix/Include");
				string PixLibDir = Target.UEThirdPartySourceDirectory + "/Windows/Pix/Lib/" + Arch + "/";
				PublicDelayLoadDLLs.Add("WinPixEventRuntime.dll");
				PublicAdditionalLibraries.Add(PixLibDir + "WinPixEventRuntime.lib");
				RuntimeDependencies.Add(System.String.Format("$(EngineDir)/Binaries/ThirdParty/Windows/WinPixEventRuntime/{0}/WinPixEventRuntime.dll", Arch));
				PublicDefinitions.Add("D3D12_PROFILING_ENABLED=1");
				PublicDefinitions.Add("PROFILE");
			}
			else
			{
				PublicDefinitions.Add("D3D12_PROFILING_ENABLED=0");
			}
		}

		// D3D12Core runtime. Currently x64 only, but ARM64 can also be supported if necessary.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDefinitions.Add("D3D12_CORE_ENABLED=1");

			// Copy D3D12Core binaries to the target directory, so it can be found by D3D12.dll loader.
			// D3D redistributable search path is configured in LaunchWindows.cpp like so:			
			// 		extern "C" { _declspec(dllexport) extern const UINT D3D12SDKVersion = 4; }
			// 		extern "C" { _declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

			// NOTE: We intentionally put D3D12 redist binaries into a subdirectory.
			// System D3D12 loader will be able to pick them up using D3D12SDKPath export, if running on compatible Win10 version.
			// If we are running on incompatible/old system, we don't want those libraries to ever be loaded.
			// A specific D3D12Core.dll is only compatible with a matching d3d12SDKLayers.dll counterpart.
			// If a wrong d3d12SDKLayers.dll is present in PATH, it will be blindly loaded and the engine will crash.

			RuntimeDependencies.Add(
				System.String.Format("$(TargetOutputDir)/D3D12/D3D12Core.dll"), 
				System.String.Format("$(EngineDir)/Binaries/ThirdParty/Windows/DirectX/x64/D3D12Core.dll"));

			if (Target.Configuration != UnrealTargetConfiguration.Shipping &&
				Target.Configuration != UnrealTargetConfiguration.Test)
			{
				RuntimeDependencies.Add(
					System.String.Format("$(TargetOutputDir)/D3D12/d3d12SDKLayers.dll"), 
					System.String.Format("$(EngineDir)/Binaries/ThirdParty/Windows/DirectX/x64/d3d12SDKLayers.dll"));
			}
		}
		else
		{
			PublicDefinitions.Add("D3D12_CORE_ENABLED=0");
		}

		// Always delay-load D3D12
		PublicDelayLoadDLLs.AddRange( new string[] {
			"d3d12.dll"
			} );

		if (LibDir != null)
		{
			PublicSystemIncludePaths.Add(DirectXSDKDir + "/include");

			PublicAdditionalLibraries.AddRange(
				new string[] {
					LibDir + "d3d12.lib"
				}
				);
		}

		//DX12 extensions, not part of SDK
		string D3DX12Dir = Target.UEThirdPartySourceDirectory + "Windows/D3DX12";
		PublicSystemIncludePaths.Add(D3DX12Dir + "/include");

	}
}

