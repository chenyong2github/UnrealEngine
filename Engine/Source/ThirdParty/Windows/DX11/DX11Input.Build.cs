// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class DX11Input : ModuleRules
{
	public DX11Input(ReadOnlyTargetRules Target) : base(Target)
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
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/x86");
		}

		PublicAdditionalLibraries.AddRange(
			new string[] {
				"dxguid.lib",
				"dinput8.lib"
			}
			);
	}
}

