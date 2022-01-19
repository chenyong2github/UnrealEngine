// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class LowLevelTestsTarget : TargetRules
{
	public LowLevelTestsTarget(TargetInfo Target) : base(Target)
	{
		ExeBinariesSubFolder = LaunchModuleName = "LowLevelTests";

		SolutionDirectory = "Programs/LowLevelTests";

		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;

		bDeployAfterCompile = false;
		bIsBuildingConsoleApplication = true;

		// Disabling default true flags that aren't necessary for tests

		// Lean and Mean mode
		bBuildDeveloperTools = false;
		
		// No localization
		bCompileICU = false;

		// No need for shaders by default
		bForceBuildShaderFormats = false;

		// Do not link against the engine, no Chromium Embedded Framework etc.
		bCompileAgainstEngine = false;
		bCompileCEF3 = false;
		bCompileAgainstCoreUObject = false;
		bCompileAgainstApplicationCore = false;
		bUseLoggingInShipping = true;

		bool bDebugOrDevelopment = Target.Configuration == UnrealTargetConfiguration.Debug || Target.Configuration == UnrealTargetConfiguration.Development;
		bBuildWithEditorOnlyData = Target.Platform.IsInGroup(UnrealPlatformGroup.Desktop) && bDebugOrDevelopment;

		// Disable malloc profiling in tests
		bUseMallocProfiler = false;

		// Useful for debugging test failures
		if (Target.Configuration == UnrealTargetConfiguration.Debug)
		{
			bDebugBuildsActuallyUseDebugCRT = true;
		}

		GlobalDefinitions.Add("STATS=0");

		// Platform specific setup
		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			UndecoratedConfiguration = Target.Configuration;

			string VersionScriptFile = Path.Combine(Directory.GetCurrentDirectory(), @"Developer\LowLevelTestsRunner\Private\Platform\Android\HideSymbols.ldscript");
			AdditionalLinkerArguments = " -Wl,--version-script=" + VersionScriptFile;

			GlobalDefinitions.Add("USE_ANDROID_INPUT=0");
			GlobalDefinitions.Add("USE_ANDROID_OPENGL=0");
			GlobalDefinitions.Add("USE_ANDROID_LAUNCH=0");
			GlobalDefinitions.Add("USE_ANDROID_JNI=0");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			GlobalDefinitions.Add("HAS_METAL=0");

			bIsBuildingConsoleApplication = false;
			// Required for IOS, but needs to fix compilation errors
			bCompileAgainstApplicationCore = true;
		}
	}

	protected void SetupModule()
	{
		LaunchModuleName = this.GetType().Name.Replace("Target", string.Empty);
		ExeBinariesSubFolder = LaunchModuleName;
	}
}
