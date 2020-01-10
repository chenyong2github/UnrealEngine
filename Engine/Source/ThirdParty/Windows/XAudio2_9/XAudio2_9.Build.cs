// Copyright Epic Games, Inc. All Rights Reserved.
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
			PublicAdditionalLibraries.Add(XAudio2_9Dir + "/Lib/x64/xaudio2_9redist.lib");
            RuntimeDependencies.Add("$(TargetOutputDir)/XAudio2_9redist.dll", XAudio2_9Dir + "/Bin/x64/XAudio2_9redist.dll");
        }
        else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicAdditionalLibraries.Add(XAudio2_9Dir + "/Lib/x86/xaudio2_9redist.lib");
            RuntimeDependencies.Add("$(TargetOutputDir)/XAudio2_9redist.dll", XAudio2_9Dir + "/Bin/x86/XAudio2_9redist.dll");
        }

    }
}

