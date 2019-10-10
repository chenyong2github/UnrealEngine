// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public abstract class DatasmithRevitBaseTarget : TargetRules
{
	public DatasmithRevitBaseTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Program;
		SolutionDirectory = "Programs/Enterprise";

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
	}

	protected void AddPostBuildSteps(string ProjectName, string RevitAPIName)
	{
		string RevitExporterPath = @"$(EngineDir)\Source\Programs\Enterprise\Datasmith\DatasmithRevitExporter";
		string RevitAPILocation = Path.Combine(RevitExporterPath, "NotForLicensees", RevitAPIName);
		string ProjectFile = Path.Combine(RevitExporterPath, ProjectName, ProjectName+".csproj");
		string BuildCommand = string.Format(@"$(EngineDir)\Build\BatchFiles\MSBuild.bat /t:Build /p:Configuration=Release /p:{1}=%{1}% {0}", ProjectFile, RevitAPIName);
		string ErrorMsg = string.Format("Cannot build {0}: Environment variable {1} is not defined.", ProjectName, RevitAPIName);

		// Since the Datasmith Revit Exporter is a C# project, build in batch the release configuration of the Visual Studio C# project file.
		// Outside of Epic Games, environment variable <RevitAPIName> must be set to the Revit API directory on the developer's workstation.
		PostBuildSteps.Add("setlocal enableextensions");
		PostBuildSteps.Add(string.Format(@"if not defined {0} (if exist {1} (set {0}={1}) else ((echo {2}) & (exit /b 1)))", RevitAPIName, RevitAPILocation, ErrorMsg));
		PostBuildSteps.Add(string.Format(@"echo {0}", BuildCommand));
		PostBuildSteps.Add(BuildCommand);
	}
}

public class DatasmithRevit2018Target : DatasmithRevitBaseTarget
{
	public DatasmithRevit2018Target(TargetInfo Target)
		: base(Target)
	{
		LaunchModuleName = "DatasmithRevit2018";
		ExeBinariesSubFolder = Path.Combine("Revit", "2018");

		AddPostBuildSteps(LaunchModuleName, "Revit_2018_API");
	}
}
