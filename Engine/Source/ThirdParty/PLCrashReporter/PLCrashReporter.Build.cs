// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class PLCrashReporter : ModuleRules
{
	public PLCrashReporter(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string PLVersion = "plcrashreporter-master-5ae3b0a";
		string[] PLDefines = new string[] {};

		// ONLY FOR TESTING 
		// This version builds for Mac arm64 but does not include the custom changes made to the version above
		// Those either need reintegrated into the version below, or the changes in the version below 
		// that cleanup all the project of old architectures and build steps could be recreated in the current
		// version.
		//PLVersion = "plcrashreporter-master-0c55d20-2020_07_10";
		//PLDefines = new[] {"USE_UNTESTED_PL_CRASHREPORTER"};

		string PLCrashReporterPath = Path.Combine(Target.UEThirdPartySourceDirectory,"PLCrashReporter",PLVersion);
		string PLSourcePath = Path.Combine(PLCrashReporterPath, "Source");
		string PLLibPath = Path.Combine(PLCrashReporterPath, "lib");

		if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicSystemIncludePaths.Add(PLSourcePath);

			string LibConfig = "Release";

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				LibConfig = "Debug";
			}
			else
			{
				LibConfig = "Release";
			}

			string Lib = Path.Combine(PLLibPath, Target.Platform.ToString(), LibConfig, "libCrashReporter.a");				
			PublicAdditionalLibraries.Add(Lib);		

			PublicDefinitions.AddRange(PLDefines);	
		}
    }
}
