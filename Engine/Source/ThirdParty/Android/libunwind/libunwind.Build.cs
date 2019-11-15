// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class libunwind : ModuleRules
{
	public libunwind(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			string libunwindLibraryPath = Target.UEThirdPartySourceDirectory + "Android/libunwind/Android/Release/";
			string libunwindIncludePath = Target.UEThirdPartySourceDirectory + "Android/libunwind/libunwind/include/";
			PublicIncludePaths.Add(libunwindIncludePath);
			PublicLibraryPaths.Add(libunwindLibraryPath + "arm64-v8a");
			PublicLibraryPaths.Add(libunwindLibraryPath + "armeabi-v7a");
			PublicLibraryPaths.Add(libunwindLibraryPath + "x86");
			PublicLibraryPaths.Add(libunwindLibraryPath + "x64");
			PublicAdditionalLibraries.Add("unwind");
			PublicAdditionalLibraries.Add("unwindbacktrace");
		}
    }
}
