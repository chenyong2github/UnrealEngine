// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class DX12 : ModuleRules
{
	public DX12(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		// @ATG_CHANGE : BEGIN HoloLens support - using the flattened "DirectX" folder that has some conflicting legacy items is an issue when consuming W10 SDK
		string DirectXSDKDir = Target.UEThirdPartySourceDirectory + "Windows/DirectX";
        string Arch = Target.WindowsPlatform.GetArchitectureSubpath();
		// @ATG_CHANGE : END
		PublicSystemIncludePaths.Add(DirectXSDKDir + "/include");

        bool PixAvalable = false;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/" + Arch);

			PixAvalable = (Target.WindowsPlatform.Architecture == WindowsArchitecture.x64);
        }
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/" + Arch);
		}
		// @ATG_CHANGE : BEGIN HoloLens support
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
            PixAvalable = (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64);
        }

		if(PixAvalable &&
            //Target.WindowsPlatform.bUseWindowsSDK10 &&
            Target.WindowsPlatform.bPixProfilingEnabled &&
            Target.Configuration != UnrealTargetConfiguration.Shipping &&
            Target.Configuration != UnrealTargetConfiguration.Test)
        {
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
        // @ATG_CHANGE : END

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

