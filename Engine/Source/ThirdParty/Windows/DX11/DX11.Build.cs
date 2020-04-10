// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class DX11 : ModuleRules
{
	public DX11(ReadOnlyTargetRules Target) : base(Target)
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

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicSystemIncludePaths.Add(DirectXSDKDir + "/Include");

			PublicDefinitions.Add("WITH_D3DX_LIBS=1");

			string LibDir = null;
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				LibDir = DirectXSDKDir + "/Lib/x64/";
			}
			else if (Target.Platform == UnrealTargetPlatform.Win32)
			{
				LibDir = DirectXSDKDir + "/Lib/x86/";
			}

			PublicAdditionalLibraries.AddRange(
				new string[] {
					LibDir + "dxgi.lib",
					LibDir + "d3d9.lib",
					LibDir + "d3d11.lib",
					LibDir + "dxguid.lib",
					LibDir + "d3dcompiler.lib",
					(Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT) ? LibDir + "d3dx11d.lib" : LibDir + "d3dx11.lib",
					LibDir + "dinput8.lib",
					LibDir + "X3DAudio.lib",
					LibDir + "xapobase.lib",
					LibDir + "XAPOFX.lib"
					}
				);
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			PublicDefinitions.Add("WITH_D3DX_LIBS=0");
		}

		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PublicSystemIncludePaths.Add(DirectXSDKDir + "/Include");

			PublicDefinitions.Add("WITH_D3DX_LIBS=0");
			PublicSystemLibraries.AddRange(
				new string[] {
				"dxguid.lib",
				}
				);
		}

	}
}

