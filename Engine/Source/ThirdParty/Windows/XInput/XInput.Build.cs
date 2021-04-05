// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class XInput : ModuleRules
{
	public XInput(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string DirectXSDKDir = Target.UEThirdPartySourceDirectory + "Windows/DirectX";

		// Ensure correct include and link paths for xinput so the correct dll is loaded (xinput1_3.dll)

		if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PublicSystemLibraries.Add("xinputuap.lib");
			PublicSystemIncludePaths.Add(DirectXSDKDir + "/include");
		}
		else
		{
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicAdditionalLibraries.Add(DirectXSDKDir + "/Lib/x64/XInput.lib");
				PublicSystemIncludePaths.Add(DirectXSDKDir + "/include");
			}
		}
	}
}

