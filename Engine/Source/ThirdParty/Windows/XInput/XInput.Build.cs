// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class XInput : ModuleRules
{
	public XInput(ReadOnlyTargetRules Target) : base(Target)
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

		// Ensure correct include and link paths for xinput so the correct dll is loaded (xinput1_3.dll)
		PublicSystemIncludePaths.Add(DirectXSDKDir + "/include");
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/x64");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/x86");
		}
		
		if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PublicAdditionalLibraries.Add("xinputuap.lib");
		}
		else
		{
			PublicAdditionalLibraries.Add("XInput.lib");
		}
	}
}

