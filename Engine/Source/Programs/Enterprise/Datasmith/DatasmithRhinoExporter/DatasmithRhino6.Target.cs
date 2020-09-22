// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

[SupportedPlatforms("Win64")]
public abstract class DatasmithRhinoBaseTarget : TargetRules
{
	private const string RhinoExporterPath = @"$(EngineDir)\Source\Programs\Enterprise\Datasmith\DatasmithRhinoExporter";

	private string RhinoEnvVarName 
	{
		get { return "RHINO" + GetVersion() + "_PATH"; }
	}

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

		AddVersionUpdatePostSteps(Target);

		// Define post-build step
		// Since the Datasmith Rhino Exporter is a C# project, build in batch the release configuration of the Visual Studio C# project file.
		string ProjectFile = Path.Combine(RhinoExporterPath, ProjectName, ProjectName+".csproj");
		string CustomAssemblyInfoPath = Path.Combine(GetPropertiesPath(), "GeneratedAssemblyInfo.cs");
		string BuildCommand = string.Format(@"$(EngineDir)\Build\BatchFiles\MSBuild.bat /t:Build /p:Configuration={1} /p:AssemblyInfo=""{2}"" {0}", ProjectFile, Target.Configuration, CustomAssemblyInfoPath);
		string ErrorMsg = string.Format("Cannot build {0}: Environment variable {1} is not defined.", ProjectName, RhinoEnvVarName);

		// If the environment variable RHINO<version>_PATH is set we use it to find the Rhino SDK location, otherwise we look if the SDK is in the ThirdParty folder, if not we look for it in an eventual Rhino installation
		PostBuildSteps.Add("setlocal enableextensions");
		PostBuildSteps.Add(string.Format(@"if not defined {0} (if exist ""{1}"" (set ""{0}={1}"") else if exist ""{2}"" (set ""{0}={2}"") else ((echo {3}) & (exit /b 1)))", RhinoEnvVarName, GetRhinoThirdPartyFolder(), GetRhinoInstallFolder(), ErrorMsg));

		PostBuildSteps.Add(string.Format(@"echo {0}", BuildCommand));
		PostBuildSteps.Add(BuildCommand);

	}

	private static string GetPropertiesPath()
	{
		return Path.Combine("$(EngineDir)", "Source", "Programs", "Enterprise", "Datasmith", "DatasmithRhinoExporter", "Properties");
	}

	private void AddVersionUpdatePostSteps(TargetInfo Target)
	{
		ReadOnlyBuildVersion BuildVersion = Target.Version;
		string VersionString = string.Format("{0}.{1}.{2}.{3}", BuildVersion.MajorVersion, BuildVersion.MinorVersion, BuildVersion.PatchVersion, BuildVersion.Changelist);
		string CustomAssemblyInfoPath = Path.Combine(GetPropertiesPath(), "GeneratedAssemblyInfo.cs");
		string CustomAssemblyTemplatePath = Path.Combine(GetPropertiesPath(), "GeneratedAssemblyInfo.cs.Template");
		string Disclaimer = "// Warning. This is an AUTO-GENERATED file used for automatic version management. Please add your changes to GeneratedAssemblyInfo.cs.template.";

		PostBuildSteps.Add(string.Format(@"echo Generating custom AssemblyInfo.cs with assembly version: {0}", VersionString));
		PostBuildSteps.Add(string.Format(@"echo ""copy /Y {0} {1}""", CustomAssemblyTemplatePath, CustomAssemblyInfoPath));
		PostBuildSteps.Add(string.Format(@"copy /Y ""{0}"" ""{1}""", CustomAssemblyTemplatePath, CustomAssemblyInfoPath));
		// Add a disclaimer on the auto-generated file.
		PostBuildSteps.Add(string.Format(@"powershell -Command ""(Get-Content -Path ""{0}"") -replace '<AutoGenerationDisclaimer>', '{1}' | Out-File -encoding UTF8 {0}""; exit", CustomAssemblyInfoPath, Disclaimer));
		// Update the assembly version of the plugin.
		PostBuildSteps.Add(string.Format(@"powershell -Command ""(Get-Content -Path ""{0}"") -replace '<AssemblyVersion>', '{1}' | Out-File -encoding UTF8 {0}""; exit", CustomAssemblyInfoPath, VersionString));
		PostBuildSteps.Add(string.Format(@"powershell -Command ""(Get-Content -Path ""{0}"") -replace '<AssemblyFileVersion>', '{1}' | Out-File -encoding UTF8 {0}""; exit", CustomAssemblyInfoPath, VersionString));
	}

	public string GetRhinoThirdPartyFolder()
	{
		return @"$(EngineDir)\Restricted\NotForLicensees\Source\ThirdParty\Enterprise\RhinoCommonSDK_" + GetVersion();
	}

	public abstract string GetVersion();

	public abstract string GetRhinoInstallFolder();
}

public class DatasmithRhino6Target : DatasmithRhinoBaseTarget
{
	public DatasmithRhino6Target(TargetInfo Target)
		: base(Target)
	{
	}

	public override string GetVersion() { return "6"; }

	public override string GetRhinoInstallFolder()
	{
		try
		{
			return Microsoft.Win32.Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\McNeel\Rhinoceros\6.0\Install", "Path", "") as string;
		}
		catch(Exception)
		{
			return "";
		}
	}
}
