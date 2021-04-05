// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class DX9 : ModuleRules
{
	public DX9(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string DirectXSDKDir = Target.UEThirdPartySourceDirectory + "Windows/DirectX";

		string LibDir = null;
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			LibDir = DirectXSDKDir + "/Lib/x64/";
		}

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PublicSystemIncludePaths.Add(DirectXSDKDir + "/include");

			PublicAdditionalLibraries.AddRange(
				new string[] {
					LibDir + "d3d9.lib",
					LibDir + "dxguid.lib",
					LibDir + "dinput8.lib",
					LibDir + "X3DAudio.lib",
					LibDir + "xapobase.lib",
					LibDir + "XAPOFX.lib"
				}
			);
		}
	}
}

