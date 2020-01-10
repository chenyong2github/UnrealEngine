// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class DatasmithSDKTarget : TargetRules
{
	public DatasmithSDKTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Program;
		SolutionDirectory = "Programs/Datasmith";

		LaunchModuleName = "DatasmithSDK";
		ExeBinariesSubFolder = "DatasmithSDK";

		ExtraModuleNames.AddRange( new string[] { "DatasmithCore", "DatasmithExporter"} );

		LinkType = TargetLinkType.Monolithic;
		bShouldCompileAsDLL = true;

		bBuildDeveloperTools = false;
		bUseMallocProfiler = false;
		bBuildWithEditorOnlyData = true;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = true;
		bCompileICU = false;
		bUsesSlate = false;
		bDisableDebugInfo = true;
		bUsePDBFiles = true;
		bHasExports = true;
		bIsBuildingConsoleApplication = true;

		if (Platform == UnrealTargetPlatform.Win64 || Platform == UnrealTargetPlatform.Win32)
		{
			AddWindowsPostBuildSteps();
		}
	}

	public void AddWindowsPostBuildSteps()
	{
		// Copy the documentation
		string SrcPath = @"$(EngineDir)\Source\Programs\Enterprise\Datasmith\DatasmithSDK\Documentation\*.*";
		string DestPath = @"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Documentation\";

		PostBuildSteps.Add(string.Format("echo Copying {0} to {1}", SrcPath, DestPath));
		PostBuildSteps.Add(string.Format("xcopy {0} {1} /R /Y /S", SrcPath, DestPath));

		// Copy the header files
		SrcPath = @"$(EngineDir)\Source\Developer\Datasmith\DatasmithCore\Public\*.h";
		DestPath = @"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Public\";

		PostBuildSteps.Add(string.Format("echo Copying {0} to {1}", SrcPath, DestPath));
		PostBuildSteps.Add(string.Format("xcopy {0} {1} /R /Y /S", SrcPath, DestPath));

		SrcPath = @"$(EngineDir)\Source\Developer\Datasmith\DatasmithExporter\Public\*.h";
		DestPath = @"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Public\";

		PostBuildSteps.Add(string.Format("echo Copying {0} to {1}", SrcPath, DestPath));
		PostBuildSteps.Add(string.Format("xcopy {0} {1} /R /Y /S", SrcPath, DestPath));
	}
}
