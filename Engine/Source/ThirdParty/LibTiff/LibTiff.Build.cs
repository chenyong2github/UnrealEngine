// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class LibTiff : ModuleRules
{
	public LibTiff(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		bool bWithLibTiff = false;
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibPath = Path.Combine(ModuleDirectory, "Lib", Target.Platform.ToString());
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "tiff.lib"));
			PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "Source", Target.Platform.ToString()));
			bWithLibTiff = true;
		}

		if (bWithLibTiff)
		{
			PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "Source"));
			AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "LibJpegTurbo");
		}

		PublicDefinitions.Add("WITH_LIBTIFF=" + (bWithLibTiff ? '1' : '0'));
	}
}