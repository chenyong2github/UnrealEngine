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
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "turbojpeg.lib"));

			PublicDelayLoadDLLs.Add("turbojpeg.dll");
			string BinPath = Path.Combine(ModuleDirectory, "bin/Win64");
			RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", "turbojpeg.dll"), Path.Combine(BinPath, "turbojpeg.dll"));
		}

		// **** NOTE - Only Win64 has been tested - other platforms are usable at your own risk, but have not been tested

/*		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			string LibPath = Path.Combine(ModuleDirectory, "lib/Win32");
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "turbojpeg.lib"));

			PublicDelayLoadDLLs.Add("turbojpeg.dll");
			string BinPath = Path.Combine(ModuleDirectory, "bin/Win32");
			RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", "turbojpeg.dll"), Path.Combine(BinPath, "turbojpeg.dll"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string LibPath = Path.Combine(ModuleDirectory, "lib/Mac");
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libturbojpeg.dylib"));

			string BinPath = Path.Combine(ModuleDirectory, "bin/Mac");
			RuntimeDependencies.Add(Path.Combine(BinPath, "libturbojpeg.dylib"));
		}
		else if ((Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) && Target.Architecture.StartsWith("x86_64")))
		{
			string LibPath = Path.Combine(ModuleDirectory, "lib/Linux");
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libturbojpeg.so"));

			string BinPath = Path.Combine(ModuleDirectory, "bin/Linux");
			RuntimeDependencies.Add(Path.Combine(BinPath, "libturbojpeg.so"));
		}
*/
	}
}
