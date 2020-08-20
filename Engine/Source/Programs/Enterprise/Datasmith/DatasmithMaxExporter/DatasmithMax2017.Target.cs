// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms("Win64")]
public abstract class DatasmithMaxBaseTarget : TargetRules
{
	public DatasmithMaxBaseTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Program;
		SolutionDirectory = "Programs/Datasmith";
		bBuildInSolutionByDefault = false;
		bLegalToDistributeBinary = true;

		bShouldCompileAsDLL = true;
		LinkType = TargetLinkType.Monolithic;

		WindowsPlatform.ModuleDefinitionFile = "Programs/Enterprise/Datasmith/DatasmithMaxExporter/DatasmithMaxExporter.def";
		WindowsPlatform.bStrictConformanceMode = false;

		bBuildDeveloperTools = false;
		bUseMallocProfiler = false;
		bBuildWithEditorOnlyData = true;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = true;
		bCompileICU = false;
		bUsesSlate = false;

		bHasExports = true;
		bForceEnableExceptions = true;
	}

	protected void AddCopyPostBuildStep(TargetInfo Target)
	{
		// Add a post-build step that copies the output to a file with the .dle extension
		string OutputName = "$(TargetName)";
		if (Target.Configuration != UnrealTargetConfiguration.Development)
		{
			OutputName = string.Format("{0}-{1}-{2}", OutputName, Target.Platform, Target.Configuration);
		}

		string SrcOutputFileName = string.Format(@"$(EngineDir)\Binaries\Win64\{0}\{1}.dll", ExeBinariesSubFolder, OutputName);
		string DstOutputFileName = string.Format(@"$(EngineDir)\Binaries\Win64\{0}\{1}.dle", ExeBinariesSubFolder, OutputName);
		PostBuildSteps.Add(string.Format("echo Copying {0} to {1}...", SrcOutputFileName, DstOutputFileName));
		PostBuildSteps.Add(string.Format("copy /Y \"{0}\" \"{1}\" 1>nul", SrcOutputFileName, DstOutputFileName));
	}
}

public class DatasmithMax2017Target : DatasmithMaxBaseTarget
{
	public DatasmithMax2017Target(TargetInfo Target)
		: base(Target)
	{
		LaunchModuleName = "DatasmithMax2017";
		ExeBinariesSubFolder = @"3DSMax\2017";

		AddCopyPostBuildStep(Target);
	}
}
