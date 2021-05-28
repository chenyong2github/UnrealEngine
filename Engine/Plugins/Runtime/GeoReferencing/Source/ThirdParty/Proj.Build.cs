// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class PROJ : ModuleRules
{
	public PROJ(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		bEnableExceptions = true;
		string VcPkgInstalled = "vcpkg-installed";

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, VcPkgInstalled, "overlay-x64-windows", "include"));

			string LibPath = Path.Combine(ModuleDirectory, VcPkgInstalled, "overlay-x64-windows", "lib");
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "proj.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "sqlite3.lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			if (Target.WindowsPlatform.Architecture == WindowsArchitecture.x64) // emulation target, bBuildForEmulation
			{
				PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, VcPkgInstalled, "overlay-x64-uwp", "include"));

				string LibPath = Path.Combine(ModuleDirectory, VcPkgInstalled, "overlay-x64-uwp", "lib");
				PublicAdditionalLibraries.Add(Path.Combine(LibPath, "proj.lib"));
				PublicAdditionalLibraries.Add(Path.Combine(LibPath, "sqlite3.lib"));
			}
			else if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64) // device target, bBuildForDevice
			{
				PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, VcPkgInstalled, "overlay-arm64-uwp", "include"));

				string LibPath = Path.Combine(ModuleDirectory, VcPkgInstalled, "overlay-arm64-uwp", "lib");
				PublicAdditionalLibraries.Add(Path.Combine(LibPath, "proj.lib"));
				PublicAdditionalLibraries.Add(Path.Combine(LibPath, "sqlite3.lib"));
			}
			else
			{
				throw new System.Exception("Unknown architecture for HoloLens platform!");
			}
		}
		else if(Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, VcPkgInstalled, "overlay-x64-linux", "include"));

			string LibPath = Path.Combine(ModuleDirectory, VcPkgInstalled, "overlay-x64-linux", "lib");
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libproj.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libsqlite3.a"));
		}
	}
}
