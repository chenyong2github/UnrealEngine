// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;
using System.IO;

public delegate void PostCopyFunc(string SrcPath, string DestPath);

[SupportedPlatforms("Win64", "Mac", "Linux")]
public class DatasmithSDKTarget : TargetRules
{
	public DatasmithSDKTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Program;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		DefaultBuildSettings = BuildSettingsVersion.V2;
		SolutionDirectory = "Programs/Datasmith";
		bBuildInSolutionByDefault = false;

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
		bDisableDebugInfo = false;
		bUsePDBFiles = true;
		bHasExports = true;
		bIsBuildingConsoleApplication = true;

		PostCopyFunc PostBuildCopy = PostBuildCopyWin64;
		if (Platform != UnrealTargetPlatform.Win64)
		{
			PostBuildCopy = PostBuildCopyUnix;
		}

		AddPostBuildSteps(PostBuildCopy);

		// Enable UDP in shipping (used by DirectLink)
		if (BuildEnvironment == TargetBuildEnvironment.Unique)
		{
			GlobalDefinitions.Add("ALLOW_UDP_MESSAGING_SHIPPING=1"); // bypasses the 'if shipping' of UdpMessagingModule.cpp
			GlobalDefinitions.Add("PLATFORM_SUPPORTS_MESSAGEBUS=1"); // required to enable the default MessageBus in MessagingModule.cpp
		}
	}
	public void PostBuildCopyWin64(string SrcPath, string DestPath)
	{
		PostBuildSteps.Add(string.Format("echo Copying {0} to {1}", SrcPath, DestPath));
		PostBuildSteps.Add(string.Format("xcopy \"{0}\" \"{1}\" /R /Y /S /Q", SrcPath, DestPath));
	}

	public void PostBuildCopyUnix(string SrcPath, string DestPath)
	{
		SrcPath = SrcPath.Replace("\\", "/");
		DestPath = DestPath.Replace("\\", "/");
		PostBuildSteps.Add(string.Format("echo Copying {0} to {1}\n", SrcPath, DestPath));
		PostBuildSteps.Add(string.Format("mkdir -p {0}\n", DestPath));
		PostBuildSteps.Add(string.Format("cp -R -f {0} {1}\n", SrcPath, DestPath));
	}

	public void AddPostBuildSteps(PostCopyFunc PostBuildCopy)
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

		PostBuildCopy(
			@"$(EngineDir)\Source\Developer\Datasmith\DatasmithExporterUI\Public\*.h",
			@"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\Public\"
		);

		PostBuildCopy(
			@"$(EngineDir)\Extras\VisualStudioDebugging\Unreal.natvis",
			@"$(EngineDir)\Binaries\$(TargetPlatform)\DatasmithSDK\"
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
