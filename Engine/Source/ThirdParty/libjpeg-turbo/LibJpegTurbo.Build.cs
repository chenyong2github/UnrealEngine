// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class LibJpegTurbo : ModuleRules
{
	public LibJpegTurbo(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string IncPath = Path.Combine(ModuleDirectory, "include");
		PublicSystemIncludePaths.Add(IncPath);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibPath = Path.Combine(ModuleDirectory, "lib/Win64");
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "turbojpeg-static.lib"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string LibPath = Path.Combine(ModuleDirectory, "lib/Unix", Target.Architecture);

			if (Target.Configuration == UnrealTargetConfiguration.Debug)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libturbojpegd.a"));
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libturbojpeg.a"));
			}
		}

		// **** NOTE - Only Win64/Linux has been tested - other platforms are usable at your own risk, but have not been tested

/*		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string LibPath = Path.Combine(ModuleDirectory, "lib/Mac");
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libturbojpeg.dylib"));
		}
*/
	}
}
