// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class XAudio2_9 : ModuleRules
{
	public XAudio2_9(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string XAudio2_9Dir = Target.UEThirdPartySourceDirectory + "Windows/XAudio2_9";

		PublicSystemIncludePaths.Add(XAudio2_9Dir + "/Include");

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && Target.Platform != UnrealTargetPlatform.Win32)
		{
			PublicDelayLoadDLLs.Add("XAudio2_9redist.dll");
			PublicAdditionalLibraries.Add(XAudio2_9Dir + "/Lib/x64/xaudio2_9redist.lib");
			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Windows/XAudio2_9/x64/xaudio2_9redist.dll");
			//RuntimeDependencies.Add("$(TargetOutputDir)/XAudio2_9redist.pdb", XAudio2_9Dir + "/Lib/x64/XAudio2_9redist.pdb", StagedFileType.DebugNonUFS);
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicDelayLoadDLLs.Add("XAudio2_9redist.dll");
			PublicAdditionalLibraries.Add(XAudio2_9Dir + "/Lib/x86/xaudio2_9redist.lib");
			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Windows/XAudio2_9/x86/xaudio2_9redist.dll");
			//RuntimeDependencies.Add("$(TargetOutputDir)/XAudio2_9redist.pdb", XAudio2_9Dir + "/Lib/x86/XAudio2_9redist.pdb", StagedFileType.DebugNonUFS);
		}

	}
}

