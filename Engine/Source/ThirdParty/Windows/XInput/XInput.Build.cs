// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class XInput : ModuleRules
{
	public XInput(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		// @ATG_CHANGE : BEGIN HoloLens support
        string DirectXSDKDir = Target.WindowsPlatform.bUseWindowsSDK10 ?
            Target.UEThirdPartySourceDirectory + "Windows/DirectXLegacy" :
			Target.UEThirdPartySourceDirectory + "Windows/DirectX";

        PublicSystemIncludePaths.Add(DirectXSDKDir + "/include");
		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{ 
			// Ensure correct include and link paths for xinput so the correct dll is loaded (xinput1_3.dll)
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/x64");
			}
			else if (Target.Platform == UnrealTargetPlatform.Win32)
			{
				PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/x86");
			}
			PublicAdditionalLibraries.Add("XInput.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PublicAdditionalLibraries.Add("xinputuap.lib");
		}
		// @ATG_CHANGE : END
	}
}

