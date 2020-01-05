// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class DatasmithFacadeCSharpTarget : TargetRules
{
	public DatasmithFacadeCSharpTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Program;
		SolutionDirectory = "Programs/Datasmith";
		bBuildInSolutionByDefault = false;

		LaunchModuleName = "DatasmithFacadeCSharp";
		ExeBinariesSubFolder = "DatasmithFacadeCSharp";

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

		AddPreBuildSteps();
		AddPostBuildSteps();
	}

	public void AddPreBuildSteps()
	{
		// Environment variable SWIG_DIR must be set to the Swig third party directory on the developer's workstation to run swig.
		if (string.IsNullOrEmpty(System.Environment.GetEnvironmentVariable("SWIG_DIR")))
		{
			PreBuildSteps.Add("echo Environment variable SWIG_DIR is not defined.");
			return;
		}

		PreBuildSteps.Add("echo Using SWIG_DIR env. variable: %SWIG_DIR%");

		string FacadePath  = @"$(EngineDir)\Source\Developer\Datasmith\DatasmithFacade";
		string ProjectPath = @"$(EngineDir)\Source\Programs\Enterprise\Datasmith\DatasmithFacadeCSharp";
		string SwigCommand = string.Format(@"%SWIG_DIR%\swig -csharp -c++ -DSWIG_FACADE -DDATASMITHFACADE_API -I{0}\Public -I{1}\Private -o {1}\Private\DatasmithFacadeCSharp.cpp -outdir {1}\Public {1}\DatasmithFacadeCSharp.i", FacadePath, ProjectPath);

		// Clean Destination folder
		PreBuildSteps.Add(string.Format(@"del {0}\Public\*.cs", ProjectPath));

		// Generate facade with swig
		PreBuildSteps.Add(SwigCommand);
	}

	protected void AddPostBuildSteps()
	{
		string SrcPath = @"$(EngineDir)\Source\Programs\Enterprise\Datasmith\DatasmithFacadeCSharp\Public\*.cs";
		string DstPath = @"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithFacadeCSharp\Public\";

		// Copy the generated C# files.
		PostBuildSteps.Add(string.Format("echo Copying {0} to {1}", SrcPath, DstPath));
		PostBuildSteps.Add(string.Format("xcopy {0} {1} /R /S /Y", SrcPath, DstPath));
	}
}
