// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class portmidi : ModuleRules
{
	public portmidi(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicIncludePaths.Add(Target.UEThirdPartySourceDirectory + "portmidi/include");

        if (Target.Platform == UnrealTargetPlatform.Win32)
        {
            PublicAdditionalLibraries.Add(Target.UEThirdPartySourceDirectory + "portmidi/lib/Win32/portmidi.lib");
        }
        else if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Target.UEThirdPartySourceDirectory + "portmidi/lib/Win64/portmidi_64.lib");
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicAdditionalLibraries.Add(Target.UEThirdPartySourceDirectory + "portmidi/lib/Mac/libportmidi.a");
			PublicAdditionalFrameworks.Add( new Framework( "CoreAudio" ));
			PublicAdditionalFrameworks.Add( new Framework( "CoreMIDI" ));
        }
	}
}
