// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class DX11Audio : ModuleRules
{
	public DX11Audio(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

        // @ATG_CHANGE : BEGIN HoloLens Support
		string DirectXSDKDir = Target.WindowsPlatform.bUseWindowsSDK10 ?
			Target.UEThirdPartySourceDirectory + "Windows/DirectXLegacy" :
			Target.UEThirdPartySourceDirectory + "Windows/DirectX";        
        // @ATG_CHANGE : END

		PublicSystemIncludePaths.Add(DirectXSDKDir + "/include");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/x64");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/x86");
		}

		// @ATG_CHANGE : BEGIN HoloLens Support
		PublicAdditionalLibraries.AddRange(
			new string[]
			{
				"dxguid.lib",
				"xapobase.lib"
			}
			);

		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.AddRange(
				new string[] {
				"X3DAudio.lib",
				"XAPOFX.lib"
				}
				);
		}
		// @ATG_CHANGE : END
	}
}

