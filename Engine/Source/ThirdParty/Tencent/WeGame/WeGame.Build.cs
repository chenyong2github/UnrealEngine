// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;

public class WeGame : ModuleRules
{
	private string TenDllPath
	{
		get { return "$(ProjectDir)/Binaries/ThirdParty/Tencent/"; }
	}

	private string RailSdkPath
	{
		get { return Path.GetFullPath(Path.Combine(Target.UEThirdPartySourceDirectory, "Tencent/WeGame/railSDK/")); }
	}
	public WeGame(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;		

		// RailSDK (Wegame platform)
		bool bRailEnabled = true;
		bool bValidTargetRail = Target.Type != TargetRules.TargetType.Server;
		bool bValidPlatformRail = Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32;
		if (bRailEnabled && bValidTargetRail && bValidPlatformRail)
		{
			PublicDefinitions.Add("WITH_TENCENT_RAIL_SDK=1");

			// add header include
			PublicSystemIncludePaths.Add(RailSdkPath);
			// add dll dependencies
			List<string> DLLNames = new List<string>();
			DLLNames.AddRange(
				new string[] {
					"rail_api.dll",
					"rail_sdk_wegame_platform.dll"
			});
			foreach (string DLLNameEntry in DLLNames)
			{
				string DLLName = DLLNameEntry;
				if (Target.Platform == UnrealTargetPlatform.Win64)
				{
					DLLName = DLLName.Replace(".dll", "64.dll");
				}
				if(Target.ProjectFile != null)
				{
					// Can't add this DLL as a dependency of the base editor
					if (Target.Platform == UnrealTargetPlatform.Win64)
					{
						RuntimeDependencies.Add(Path.Combine(TenDllPath, "Win64", DLLName));
					}
					else if (Target.Platform == UnrealTargetPlatform.Win32)
					{
						RuntimeDependencies.Add(Path.Combine(TenDllPath, "Win32", DLLName));
					}
				}
				PublicDelayLoadDLLs.Add(DLLName);
			}
		}
		else
		{
			PublicDefinitions.Add("WITH_TENCENT_RAIL_SDK=0");
		}
	}
}
