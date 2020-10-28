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
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			LibDir = DirectXSDKDir + "/Lib/x86/";
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

