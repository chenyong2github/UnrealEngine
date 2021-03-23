// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;

public class PLCrashReporter : ModuleRules
{
	public PLCrashReporter(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string PLGitRepoRoot = "PLCrashReporter";

		string[] PLDefines = new string[] {};

		string PLCrashReporterPath = Path.Combine(Target.UEThirdPartySourceDirectory,"PLCrashReporter");
		string PLSourcePath = Path.Combine(PLCrashReporterPath,PLGitRepoRoot,"Source");
		string LibConfig = "Release";

		string XcodeVersionOutput = Utils.RunLocalProcessAndReturnStdOut("xcodebuild", "-version");
		string XcodeVersion;
		using (var Reader = new StringReader(XcodeVersionOutput.Substring(6)))
		{
			XcodeVersion = Reader.ReadLine();
		}

		string[] VersionComponents = XcodeVersion.Split('.');
		string CurrentLibFolder = "lib-Xcode-" + XcodeVersion;
		string PLLibPath = Path.Combine(PLCrashReporterPath, "lib", CurrentLibFolder);

		if ( !Directory.Exists( Path.Combine(Directory.GetCurrentDirectory(), PLLibPath) ) )
		{
			string DefaultLibFolder = "lib-Xcode-12.4";
			if ( VersionComponents[0] == "11" )
			{
				DefaultLibFolder = "lib-Xcode-11.3.1";
			}

			Log.TraceInformationOnce("Couldn't find PLCrashReporter in folder '{0}', using default '{1}'", CurrentLibFolder, DefaultLibFolder);
			PLLibPath = Path.Combine(PLCrashReporterPath, "lib", DefaultLibFolder);
		}

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
    }
}
