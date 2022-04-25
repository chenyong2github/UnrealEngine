// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Diagnostics;
using System.Collections.Generic;
using System.IO;

public class OpenVDB : ModuleRules
{
	public OpenVDB(ReadOnlyTargetRules Target) : base(Target)
	{
		// We are just setting up paths for pre-compiled binaries.
		Type = ModuleType.External;

		// OpenVDB makes use of both RTTI and C++ exceptions, so clients of
		// this module should likely enable both of these as well.
		bUseRTTI = true;
		bEnableExceptions = true;

		const string versionString = "openvdb-8.1.0";
		
		string OpenVdbDir = Path.Combine(ModuleDirectory, "Deploy", versionString);
		string OpenVdbIncludeDir = Path.Combine(OpenVdbDir, "include");
		string OpenVdbLibDir = Path.Combine(OpenVdbDir, "lib");

		PublicDefinitions.Add("OPENVDB_STATICLIB");
		PublicDefinitions.Add("OPENVDB_OPENEXR_STATICLIB");
		PublicDefinitions.Add("NOMINMAX");
		
		PublicIncludePaths.Add(OpenVdbIncludeDir);

		// Only building for Windows
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			const string openVdbWin64LibName = "libopenvdb.lib";
			string OpenVdbWin64LibDir = Path.Combine(OpenVdbLibDir, "Win64", "VS2019");
			string OpenVdbWin64Lib = Path.Combine(OpenVdbWin64LibDir, openVdbWin64LibName);
			
			PublicAdditionalLibraries.Add(OpenVdbWin64Lib);
		}
		else
		{
			const string error = "OpenVDB can only be used under Windows 64-bit!";
			System.Console.WriteLine(error);
			throw new BuildException(error);
		}

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Boost"
			}
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"IntelTBB"
			}
		);

		AddEngineThirdPartyPrivateStaticDependencies(Target, "Blosc");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
	}
}
