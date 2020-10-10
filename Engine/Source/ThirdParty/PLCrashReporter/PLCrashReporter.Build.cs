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

		string PLCrashReporterPath = Path.Combine(Target.UEThirdPartySourceDirectory,"PLCrashReporter",PLVersion);
		string PLSourcePath = Path.Combine(PLCrashReporterPath, "Source");
		string PLLibPath = Path.Combine(PLCrashReporterPath, "lib");
		string LibConfig = "Release";

		if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicSystemIncludePaths.Add(PLSourcePath);

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
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string Lib = Path.Combine(PLLibPath, Target.Platform.ToString(), LibConfig, "libprotobuf-c.a");
			PublicAdditionalLibraries.Add(Lib);
		}
    }
}
