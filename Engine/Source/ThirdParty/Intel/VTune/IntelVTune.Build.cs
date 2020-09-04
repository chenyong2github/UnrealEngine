// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IntelVTune : ModuleRules
{
	public IntelVTune(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string IntelVTunePath = Target.UEThirdPartySourceDirectory + "Intel/VTune/VTune-2019/";

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			string PlatformName = (Target.Platform == UnrealTargetPlatform.Win32) ? "Win32" : "Win64";

			PublicSystemIncludePaths.Add(IntelVTunePath + "include/");

			string LibDir = IntelVTunePath + "lib/" + PlatformName + "/";

			PublicAdditionalLibraries.Add(LibDir + "libittnotify.lib");
		}
	}
}