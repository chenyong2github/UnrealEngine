// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class DatasmithCADRuntimeTarget : TargetRules
{
	public DatasmithCADRuntimeTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Program;
		SolutionDirectory = "Programs/Datasmith";

		LaunchModuleName = "DatasmithCADRuntime";
		SolutionDirectory = "Programs/Datasmith";

		//ExtraModuleNames.AddRange(new string[] { "CADInterfaces" });
		GlobalDefinitions.Add("UE_EXTERNAL_PROFILING_ENABLED=0");

		LinkType = TargetLinkType.Monolithic;
		bShouldCompileAsDLL = true;

		bBuildDeveloperTools = false;
		bBuildWithEditorOnlyData = false;

		bCompileChaos = false;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = false;
		bCompileICU = false;

		bDisableDebugInfo = false;

		bUseMallocProfiler = false;
		bUsePDBFiles = true;
		bUsesSlate = false;

		bHasExports = true;
		bIsBuildingConsoleApplication = true;
		bLegalToDistributeBinary = true;

		string EngineDir = @"..\.."; // relative to default destination, which is $(EngineDir)\Binaries\Win64
		ExeBinariesSubFolder = EngineDir + @"\Plugins\Enterprise\DatasmithCADImporter\Binaries\Win64\";
	}
}
