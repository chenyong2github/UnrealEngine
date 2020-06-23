// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public abstract class DatasmithRhinoBaseTarget : TargetRules
{
	public DatasmithRhinoBaseTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Program;
		SolutionDirectory = "Programs/Datasmith";
		bBuildInSolutionByDefault = false;

		string RhinoVersionString = GetVersion();
		string ProjectName = "DatasmithRhino" + RhinoVersionString;
		
		ExeBinariesSubFolder = Path.Combine("Rhino", RhinoVersionString);
		LaunchModuleName = ProjectName;

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

		string RhinoLibraryLocation = "";
		string RhinoRootEnvVar = "RHINO" + RhinoVersionString + "_PATH";

		// Try with custom setup
		string Location = System.Environment.GetEnvironmentVariable(RhinoRootEnvVar);
		if (Location != null && Location != "")
		{
			RhinoLibraryLocation = Location;
		}

		if (!Directory.Exists(RhinoLibraryLocation))
		{
			// Try with build machine setup
			string SDKRootEnvVar = System.Environment.GetEnvironmentVariable("UE_SDKS_ROOT");
			if (SDKRootEnvVar != null && SDKRootEnvVar != "")
			{
				RhinoLibraryLocation = Path.Combine(SDKRootEnvVar, "HostWin64", "Win64", "Rhino", RhinoVersionString);
			}
		}

		string RhinoExporterPath = @"$(EngineDir)\Source\Programs\Enterprise\Datasmith\DatasmithRhinoExporter";
		string ProjectFile = Path.Combine(RhinoExporterPath, ProjectName, ProjectName+".csproj");
		string BuildCommand = string.Format(@"$(EngineDir)\Build\BatchFiles\MSBuild.bat /t:Build /p:Configuration={1} {0}", ProjectFile, Target.Configuration);
		string ErrorMsg = string.Format("Cannot build {0}: Environment variable {1} is not defined.", ProjectName, RhinoRootEnvVar);

		// Since the Datasmith Revit Exporter is a C# project, build in batch the release configuration of the Visual Studio C# project file.
		// Outside of Epic Games, environment variable <RhinoRootEnvVar> (RHINO<year>_PATH) must be set to the Rhino directory on the developer's workstation.
		PostBuildSteps.Add("setlocal enableextensions");
		PostBuildSteps.Add(string.Format(@"if not defined {0} (if exist ""{1}"" (set {0}=""{1}"") else ((echo {2}) & (exit /b 1)))", RhinoRootEnvVar, RhinoLibraryLocation, ErrorMsg));
		PostBuildSteps.Add(string.Format(@"echo {0}", BuildCommand));
		PostBuildSteps.Add(BuildCommand);
	}

	public abstract string GetVersion();
}

public class DatasmithRhino5Target : DatasmithRhinoBaseTarget
{
	public DatasmithRhino5Target(TargetInfo Target)
		: base(Target)
	{
	}

	public override string GetVersion() { return "5"; }
}
