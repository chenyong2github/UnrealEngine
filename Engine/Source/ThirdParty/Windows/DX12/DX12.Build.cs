// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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
		PublicSystemIncludePaths.Add(DirectXSDKDir + "/include");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/x64");

            PublicDelayLoadDLLs.Add("WinPixEventRuntime.dll");
            PublicAdditionalLibraries.Add("WinPixEventRuntime.lib");
            RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Windows/DirectX/x64/WinPixEventRuntime.dll");
        }
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/x86");
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			bool PixAvalable = (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64);
			if (PixAvalable &&
			//Target.WindowsPlatform.bUseWindowsSDK10 &&
			Target.WindowsPlatform.bPixProfilingEnabled &&
			Target.Configuration != UnrealTargetConfiguration.Shipping &&
			Target.Configuration != UnrealTargetConfiguration.Test)
			{
				string Arch = Target.WindowsPlatform.GetArchitectureSubpath();
				PublicSystemIncludePaths.Add(Target.UEThirdPartySourceDirectory + "/Windows/Pix/Include");
				PublicLibraryPaths.Add(Target.UEThirdPartySourceDirectory + "/Windows/Pix/Lib/" + Arch);
				PublicDelayLoadDLLs.Add("WinPixEventRuntime.dll");
				PublicAdditionalLibraries.Add("WinPixEventRuntime.lib");
				RuntimeDependencies.Add(System.String.Format("$(EngineDir)/Binaries/ThirdParty/Windows/DirectX/{0}/WinPixEventRuntime.dll", Arch));
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

		PublicAdditionalLibraries.AddRange(
			new string[] {
                "d3d12.lib"
			}
			);
	}
}

