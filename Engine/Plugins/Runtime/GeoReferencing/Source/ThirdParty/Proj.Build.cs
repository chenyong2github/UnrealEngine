// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class PROJ : ModuleRules
{
	public PROJ(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		bEnableExceptions = true;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "vcpkg_installed", "x64-windows-static-md-v140", "include"));

			string LibPath = Path.Combine(ModuleDirectory, "vcpkg_installed", "x64-windows-static-md-v140", "lib");
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "proj.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "sqlite3.lib"));
		}
		else if(Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "vcpkg_installed", "x64-linux", "include"));

			string LibPath = Path.Combine(ModuleDirectory, "vcpkg_installed", "x64-linux", "lib");
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libproj.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libsqlite3.a"));
		}
	}
}
