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

		ExtraModuleNames.AddRange( new string[] { "DatasmithCore", "DatasmithExporter" } );

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

	public void PostBuildCopy(string SrcPath, string DestPath)
	{
		PostBuildSteps.Add(string.Format("echo Copying {0} to {1}", SrcPath, DestPath));
		PostBuildSteps.Add(string.Format("xcopy {0} {1} /R /Y /S", SrcPath, DestPath));
	}

	public void AddWindowsPostBuildSteps()
	{
		// Copy the documentation
		PostBuildCopy(
			@"$(EngineDir)\Source\Programs\Enterprise\Datasmith\DatasmithSDK\Documentation\*.*",
			@"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Documentation\"
		);

		// Package our public headers
		PostBuildCopy(
			@"$(EngineDir)\Source\Runtime\Datasmith\DatasmithCore\Public\*.h",
			@"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Public\"
		);

		PostBuildCopy(
			@"$(EngineDir)\Source\Runtime\Datasmith\DirectLink\Public\*.h",
			@"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Public\"
		);

		PostBuildCopy(
			@"$(EngineDir)\Source\Developer\Datasmith\DatasmithExporter\Public\*.h",
			@"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Public\"
		);

		// Other headers we depend on, but that are not part of our public API:
		PostBuildCopy(
			@"$(EngineDir)\Source\Runtime\TraceLog\Public\*.h",
			@"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Private\"
		);
		PostBuildCopy(
			@"$(EngineDir)\Source\Runtime\TraceLog\Public\*.inl",
			@"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Private\"
		);

		PostBuildCopy(
			@"$(EngineDir)\Source\Runtime\Messaging\Public\*.h",
			@"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Private\"
		);

		PostBuildCopy(
			@"$(EngineDir)\Source\Runtime\Core\Public\*.h",
			@"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Private\"
		);

		PostBuildCopy(
			@"$(EngineDir)\Source\Runtime\Core\Public\*.inl",
			@"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Private\"
		);

		PostBuildCopy(
			@"$(EngineDir)\Source\Runtime\CoreUObject\Public\*.h",
			@"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Private\"
		);
	}
}
