// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

[SupportedPlatforms("Win64", "Mac")]
public abstract class DatasmithArchiCADBaseTarget : TargetRules
{
	public DatasmithArchiCADBaseTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Program;
		SolutionDirectory = "Programs/Datasmith";
		bBuildInSolutionByDefault = false;

		string ArchiCADVersionString = GetVersion();

		ExeBinariesSubFolder = Path.Combine("DatasmithArchiCADExporter", "ArchiCAD" + ArchiCADVersionString);
		LaunchModuleName = "DatasmithArchiCAD" + ArchiCADVersionString;

		bShouldCompileAsDLL = true;
		LinkType = TargetLinkType.Monolithic;

		bBuildDeveloperTools = false;
		bUseMallocProfiler = false;
		bBuildWithEditorOnlyData = true;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = true;
		bCompileICU = false;
		bUsesSlate = false;

		bHasExports = true;
		bForceEnableExceptions = true;

		// Define post-build step
		string ArchiCADExporterPath = @"$(EngineDir)\Source\Programs\Enterprise\Datasmith\DatasmithARCHICADExporter";
		string SolutionPath = Path.Combine(ArchiCADExporterPath, @"Build\DatasmithARCHICADExporter.sln");
		string BuildCommand = string.Format(@"$(EngineDir)\Build\BatchFiles\MSBuild.bat /t:Build /p:Configuration=Release{1} {0}", SolutionPath, ArchiCADVersionString);
		PreBuildSteps.Add(BuildCommand);
	}

	public abstract string GetVersion();

}

[SupportedPlatforms("Win64", "Mac")]
public class DatasmithArchiCAD23Target : DatasmithArchiCADBaseTarget
{
	public DatasmithArchiCAD23Target(TargetInfo Target)
		: base(Target)
	{
	}

	public override string GetVersion() { return "23"; }
}
