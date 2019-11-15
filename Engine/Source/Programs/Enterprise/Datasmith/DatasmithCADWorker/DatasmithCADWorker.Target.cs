// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public class DatasmithCADWorkerTarget : TargetRules
{
	public DatasmithCADWorkerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		LaunchModuleName = "DatasmithCADWorker";
		SolutionDirectory = "Programs/Datasmith";

        // Lean and mean
        bBuildDeveloperTools = false;

		// Never use malloc profiling in Unreal Header Tool.  We set this because often UHT is compiled right before the engine
		// automatically by Unreal Build Tool, but if bUseMallocProfiler is defined, UHT can operate incorrectly.
		bUseMallocProfiler = false;

		// Editor-only data, however, is needed
		bBuildWithEditorOnlyData = true;

		// Currently this app is not linking against the engine, so we'll compile out references from Core to the rest of the engine
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = false;
		bCompileAgainstApplicationCore = true;

		// This is a console application, not a Windows app (sets entry point to main(), instead of WinMain())
		bIsBuildingConsoleApplication = true;

		bLegalToDistributeBinary = true;

		bCompilePhysX = false;
		bCompileAPEX = false;
		bCompileNvCloth = false;
		bCompileICU = false;
		bCompileCEF3 = false;

		string EngineDir = @"..\.."; // relative to default destination, which is $(EngineDir)\Binaries\Win64
		ExeBinariesSubFolder = EngineDir + @"\Plugins\Enterprise\DatasmithCADImporter\Binaries\Win64\";
	}
}
