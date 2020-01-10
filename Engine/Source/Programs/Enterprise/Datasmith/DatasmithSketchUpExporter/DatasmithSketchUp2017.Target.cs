// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public abstract class DatasmithSketchUpBaseTarget : TargetRules
{
	public DatasmithSketchUpBaseTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Program;
		SolutionDirectory = "Programs/Datasmith";
		bBuildInSolutionByDefault = false;

		bShouldCompileAsDLL = true;
		LinkType = TargetLinkType.Monolithic;

		WindowsPlatform.ModuleDefinitionFile = "Programs/Enterprise/Datasmith/DatasmithSketchUpExporter/DatasmithSketchUpExporter.def";

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

	protected void AddCopyPostBuildStep(TargetInfo Target, string DLLName)
	{
		// Since SketchUp expects an exporter DLL name based on the exported file extension,
		// add a post-build step that copies the output to such properly named DLL.

		string OutputName = "$(TargetName)";
		if (Target.Configuration != UnrealTargetConfiguration.Development)
		{
			OutputName = string.Format("{0}-{1}-{2}", OutputName, Target.Platform, Target.Configuration);
		}

		string SrcOutputFileName = string.Format(@"$(EngineDir)\Binaries\Win64\{0}\{1}.dll", ExeBinariesSubFolder, OutputName);
		string DstOutputFileName = string.Format(@"$(EngineDir)\Binaries\Win64\{0}\{1}.dll", ExeBinariesSubFolder, DLLName);

		PostBuildSteps.Add(string.Format("echo Copying {0} to {1}...", SrcOutputFileName, DstOutputFileName));

		// Note the * at the end allows to skip the prompt asking if the destination is a file or directory when it doesn't exist yet
		PostBuildSteps.Add(string.Format("xcopy /Y /R \"{0}\" \"{1}*\" 1>nul", SrcOutputFileName, DstOutputFileName));
	}
}

public class DatasmithSketchUp2017Target : DatasmithSketchUpBaseTarget
{
	public DatasmithSketchUp2017Target(TargetInfo Target)
		: base(Target)
	{
		LaunchModuleName = "DatasmithSketchUp2017";
		ExeBinariesSubFolder = @"SketchUp\2017";

		AddCopyPostBuildStep(Target, "skp2udatasmith2017");
	}
}
