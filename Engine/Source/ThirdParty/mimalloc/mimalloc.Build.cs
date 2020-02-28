// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class mimalloc : ModuleRules
{
	public mimalloc(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string miPath = Target.UEThirdPartySourceDirectory + "mimalloc\\";

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && Target.Platform != UnrealTargetPlatform.Win32)
		{
            PublicAdditionalLibraries.Add(miPath + "out\\msvc-x64\\Release\\mimalloc-static.lib");
            PublicSystemIncludePaths.Add(miPath + "include");
        }
    }
}
