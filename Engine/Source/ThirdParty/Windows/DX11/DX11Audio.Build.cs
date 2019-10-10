// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class DX11Audio : ModuleRules
{
	public DX11Audio(ReadOnlyTargetRules Target) : base(Target)
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

		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.AddRange(
			new string[] 
			{
				LibDir + "dxguid.lib",
				LibDir + "X3DAudio.lib",
				LibDir + "xapobase.lib",
				LibDir + "XAPOFX.lib"
			}
			);
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
            PublicSystemLibraries.AddRange(
			new string[]
			{
				LibDir + "dxguid.lib",
				LibDir + "xapobase.lib"
			}
			);
		}
	}
}

