// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class DX9 : ModuleRules
{
	public DX9(ReadOnlyTargetRules Target) : base(Target)
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
				LibDir + "d3d9.lib",
				LibDir + "dxguid.lib",
				LibDir + "d3dcompiler.lib",
				(Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT) ? LibDir + "d3dx9d.lib" : LibDir + "d3dx9.lib",
				LibDir + "dinput8.lib",
				LibDir + "X3DAudio.lib",
				LibDir + "xapobase.lib",
				LibDir + "XAPOFX.lib"
			}
		);
	}
}

