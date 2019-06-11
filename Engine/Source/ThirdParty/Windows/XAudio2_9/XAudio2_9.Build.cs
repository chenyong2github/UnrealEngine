// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class XAudio2_9 : ModuleRules
{
	public XAudio2_9(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string XAudio2_9Dir = Target.UEThirdPartySourceDirectory + "Windows/XAudio2_9";

		PublicSystemIncludePaths.Add(XAudio2_9Dir + "/Include");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicLibraryPaths.Add(XAudio2_9Dir + "/Lib/x64");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicLibraryPaths.Add(XAudio2_9Dir + "/Lib/x86");
		}
    }
}

