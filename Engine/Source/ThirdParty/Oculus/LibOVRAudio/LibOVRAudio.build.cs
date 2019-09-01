// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LibOVRAudio : ModuleRules
{
	public LibOVRAudio(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string SourceDirectory = Target.UEThirdPartySourceDirectory + "Oculus/LibOVRAudio/LibOVRAudio/";

		PublicIncludePaths.Add(SourceDirectory + "include");

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicAdditionalLibraries.Add(SourceDirectory + "lib/armeabi-v7a/ovraudio32");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// DLL dynamically loaded from FOculusAudioLibraryManager::LoadDll()
		}
	}
}
