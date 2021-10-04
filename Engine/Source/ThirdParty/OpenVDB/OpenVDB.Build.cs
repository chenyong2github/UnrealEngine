// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Diagnostics;
using System.Collections.Generic;
using System.IO;

public class OpenVDB : ModuleRules
{
    public OpenVDB(ReadOnlyTargetRules Target) : base(Target)
    {
		string VersionString = "openvdb-8.1.0";
		
		string OpenVDBDir = Path.Combine(Target.UEThirdPartySourceDirectory, "OpenVDB", "Deploy", VersionString);
		string OpenVDBIncludeDir = Path.Combine(OpenVDBDir, "include");
		string OpenVDBLibDir = Path.Combine(OpenVDBDir, "lib");
				
        // We are just setting up paths for pre-compiled binaries.
        Type = ModuleType.External;

       	PublicIncludePaths.Add(OpenVDBIncludeDir);
       
        // Only building for Windows
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
			string OpenVDBWin64LibName = "libopenvdb.lib";
			string OpenVDBWin64LibDir = Path.Combine(OpenVDBLibDir, "Win64");
			string OpenVDBWin64Lib = Path.Combine(OpenVDBWin64LibDir, OpenVDBWin64LibName);
			
            PublicAdditionalLibraries.Add(OpenVDBWin64Lib);
        }
        else
        {
            string Err = "OpenVDB can only be used under Windows 64-bit!";
            System.Console.WriteLine(Err);
            throw new BuildException(Err);
        }
		
		PublicDefinitions.Add("NOMINMAX");
		PublicDefinitions.Add("OPENVDB_STATICLIB");

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Boost"
			}
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Blosc",
				"zlib",
				"IntelTBB"
			}
		);
    }
}
