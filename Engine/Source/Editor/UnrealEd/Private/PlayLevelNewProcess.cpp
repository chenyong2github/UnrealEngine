// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/EditorEngine.h"
#include "PlayLevel.h"
#include "HeadMountedDisplayTypes.h"
#include "Editor.h"

void UEditorEngine::StartPlayInNewProcessSession(FRequestPlaySessionParams& InRequestParams)
{
	check(InRequestParams.SessionDestination == EPlaySessionDestinationType::NewProcess);

	EPlayNetMode NetMode;
	InRequestParams.EditorPlaySettings->GetPlayNetMode(NetMode);

	// Standalone requires no server, and ListenServer doesn't require a separate server.
	const bool bNetModeRequiresSeparateServer = NetMode == EPlayNetMode::PIE_Client;
	const bool bLaunchExtraServerAnyways = InRequestParams.EditorPlaySettings->bLaunchSeparateServer;
	const bool bNeedsServer = bNetModeRequiresSeparateServer || bLaunchExtraServerAnyways;
	
	bool bServerWasLaunched = false;

	if (bNeedsServer)
	{
		const bool bIsDedicatedServer = true;
		LaunchNewProcess(InRequestParams, 0, EPlayNetMode::PIE_ListenServer, bIsDedicatedServer);
		
		bServerWasLaunched = true;
	}
	
	int32 NumClients;
	InRequestParams.EditorPlaySettings->GetPlayNumberOfClients(NumClients);

	// If the have a net mode that requires a server but they didn't create (or couldn't create due to single-process
	// limitations) a dedicated one, then we launch an extra world context acting as a server in-process.
	int32 NumRequestedInstances = FMath::Max(NumClients, 1);
	for (int32 InstanceIndex = 0; InstanceIndex < NumRequestedInstances; InstanceIndex++)
	{
		EPlayNetMode LocalNetMode = NetMode;

		// If they want to launch a Listen Server and have multiple clients, the subsequent clients need to be
		// treated as Clients so they connect to the listen server instead of launching multiple Listen Servers.
		if (NetMode == EPlayNetMode::PIE_ListenServer && InstanceIndex > 0)
		{
			LocalNetMode = EPlayNetMode::PIE_Client;
		}

		// Dedicated servers should have been launched above, so this is only clients + listen servers.
		const bool bIsDedicatedServer = false;

		LaunchNewProcess(InRequestParams, InstanceIndex, LocalNetMode, bIsDedicatedServer);
	}

	// Now that we've launched the new process, we'll cancel the request so that the UI lets us go into PIE.
	// This doesn't clear our tracked sessions, so next time PIE is started it will close any standalone instances.
	CancelRequestPlaySession();
}

void UEditorEngine::LaunchNewProcess(const FRequestPlaySessionParams& InParams, const int32 InInstanceNum, EPlayNetMode NetMode, bool bIsDedicatedServer)
{
	// All dedicated servers should be considered hosts as well.
	if (bIsDedicatedServer)
	{
		NetMode = EPlayNetMode::PIE_ListenServer;
	}

	// Apply various launch arguments based on their settings.
	FString CommandLine;
	FString UnrealURLParams;

	if (bIsDedicatedServer)
	{
		CommandLine += TEXT("-server -log");
	}
	else if (NetMode == EPlayNetMode::PIE_ListenServer)
	{
		UnrealURLParams += TEXT("?Listen");

		// Add any additional url parameters the user might have specified.
		FString AdditionalServerGameOptions;
		InParams.EditorPlaySettings->GetAdditionalServerGameOptions(AdditionalServerGameOptions);

		if (AdditionalServerGameOptions.Len() > 0)
		{
			UnrealURLParams += AdditionalServerGameOptions;
		}
	}

	if(!bIsDedicatedServer)
	{
		// Allow loading specific GameUserSettings from the ini which differ per-process.
		FString GameUserSettingsOverride = GGameUserSettingsIni.Replace(TEXT("GameUserSettings"), *FString::Printf(TEXT("PIEGameUserSettings%d"), InInstanceNum));

		// Construct parms:
		//	-Override GameUserSettings.ini
		//	-Force no steam
		//	-Allow saving of config files (since we are giving them an override INI)
		CommandLine += FString::Printf(TEXT("GameUserSettingsINI=\"%s\" -MultiprocessSaveConfig -MultiprocessOSS "), *GameUserSettingsOverride);
	}

	if (bIsDedicatedServer)
	{
		// Allow server specific launch parameters. Only works with separate process standalone servers.
		CommandLine += FString::Printf(TEXT(" %s"), *InParams.EditorPlaySettings->AdditionalServerLaunchParameters);
	}

	// If they're not a host, configure the URL Params to connect to the server (instead of a specifying a map later)
	if (NetMode == EPlayNetMode::PIE_Client)
	{
		FString ServerIP = TEXT("127.0.0.1");
		uint16 ServerPort;
		InParams.EditorPlaySettings->GetServerPort(ServerPort);
		UnrealURLParams += FString::Printf(TEXT(" %s:%hu"), *ServerIP, ServerPort);
	}

	// Add Messaging and a SessionName for the Unreal Front End
	CommandLine += TEXT(" -messaging -SessionName=\"Play in Standalone Game\"");

	// Allow overriding the localization for testing other languages.
	const FString& PreviewGameLanguage = FTextLocalizationManager::Get().GetConfiguredGameLocalizationPreviewLanguage();
	if (!PreviewGameLanguage.IsEmpty())
	{
		CommandLine += TEXT(" -culture=");
		CommandLine += PreviewGameLanguage;
	}

	if (InParams.SessionPreviewTypeOverride.Get(EPlaySessionPreviewType::NoPreview) == EPlaySessionPreviewType::MobilePreview)
	{
		// Allow targeting a specific Mobile Device.
		if (InParams.MobilePreviewTargetDevice.IsSet())
		{
			CommandLine += FString::Printf(TEXT(" -MobileTargetDevice=\"%s\""), *InParams.MobilePreviewTargetDevice.GetValue());
		}
		else
		{
			// Otherwise, we'll fall back to ES2 emulation.
			CommandLine += TEXT(" -featureleveles2");
		}

		// If we're currently running in OpenGL mode, pass that onto our newly spawned processes.
		if (IsOpenGLPlatform(GShaderPlatformForFeatureLevel[GMaxRHIFeatureLevel]))
		{
			CommandLine += TEXT(" -opengl");
		}

		// Fake touch events since we're on a desktop and not a mobile device.
		CommandLine += TEXT(" -faketouches");

		// Ensure the executable writes out a differently named config file to avoid multiple instances overwriting each other.
		// ToDo: Should this be on all multi-client launches?
		CommandLine += TEXT(" -MultiprocessSaveConfig");
	}

	if (InParams.SessionPreviewTypeOverride.Get(EPlaySessionPreviewType::NoPreview) == EPlaySessionPreviewType::VulkanPreview)
	{
		// Vulkan only supports a sub-set
		CommandLine += TEXT(" -vulkan -faketouches -featureleveles31");
	}

	// If they're trying to launch a new process (from the editor) in VR, this will fail because the editor
	// owns the HMD resource, so we warn, and then fall back. They will need to use single-process for VR preview.
	if (InParams.SessionPreviewTypeOverride.Get(EPlaySessionPreviewType::NoPreview) == EPlaySessionPreviewType::VRPreview)
	{
		CommandLine += TEXT(" -nohmd");
		UE_LOG(LogHMD, Warning, TEXT("Standalone Game VR not supported, please use VR Preview."));
	}

	// Allow disabling the sound in the new clients.
	if (InParams.EditorPlaySettings->DisableStandaloneSound)
	{
		CommandLine += TEXT(" -nosound");
	}

	// Allow the user to specify their own additional launch parameters to be set.
	if (InParams.EditorPlaySettings->AdditionalLaunchParameters.Len() > 0)
	{
		CommandLine += FString::Printf(TEXT(" %s"), *InParams.EditorPlaySettings->AdditionalLaunchParameters);
	}

	// The Play in Editor request may have had its own parameters as well.
	if (InParams.AdditionalStandaloneCommandLineParameters.IsSet())
	{
		CommandLine += FString::Printf(TEXT(" %s"), *InParams.AdditionalStandaloneCommandLineParameters.GetValue());
	}

	// Mobile uses its own set of command line arguments that can be passed. Mobile can be previewed both in-process and
	// standalone, so we need two separate sets of arguments right now.
	if (InParams.EditorPlaySettings->AdditionalLaunchParametersForMobile.Len() > 0)
	{
		CommandLine += FString::Printf(TEXT(" %s"), *InParams.EditorPlaySettings->AdditionalLaunchParametersForMobile);
	}

	// Allow servers to override which port they are launched on.
	if (NetMode == EPlayNetMode::PIE_ListenServer)
	{
		uint16 ServerPort;
		InParams.EditorPlaySettings->GetServerPort(ServerPort);

		CommandLine += FString::Printf(TEXT(" -port=%hu"), ServerPort);
	}

	// Allow emulating adverse network conditions.
	if (InParams.EditorPlaySettings->IsNetworkEmulationEnabled())
	{
		NetworkEmulationTarget CurrentTarget = (NetMode == EPlayNetMode::PIE_ListenServer) ? NetworkEmulationTarget::Server : NetworkEmulationTarget::Client;
		if (InParams.EditorPlaySettings->NetworkEmulationSettings.IsEmulationEnabledForTarget(CurrentTarget))
		{
			CommandLine += InParams.EditorPlaySettings->NetworkEmulationSettings.BuildPacketSettingsForCmdLine();
		}
	}

	// Decide if fullscreen or windowed based on what is specified in the params
	if (!CommandLine.Contains(TEXT("-fullscreen")) && !CommandLine.Contains(TEXT("-windowed")))
	{
		// Nothing specified fallback to window otherwise keep what is specified
		CommandLine += TEXT(" -windowed");
	}

	if (!bIsDedicatedServer)
	{
		// Calculate a location for this window to be displayed.
		FIntPoint WindowSize, WindowPosition;
		GetWindowSizeAndPositionForInstanceIndex(*InParams.EditorPlaySettings, 0, WindowSize, WindowPosition);

		if (!InParams.EditorPlaySettings->CenterNewWindow)
		{
			// If they don't want to center the new window, we add a specific location. This will get saved to user settings
			// via SAVEWINPOS and not end up reflected in our PlayInEditor settings.
			CommandLine += FString::Printf(TEXT(" -WinX=%d -WinY=%d SAVEWINPOS=1"), InParams.EditorPlaySettings->NewWindowPosition.X, InParams.EditorPlaySettings->NewWindowPosition.Y);
		}

		// Get desktop metrics
		FDisplayMetrics DisplayMetrics;
		FSlateApplication::Get().GetCachedDisplayMetrics(DisplayMetrics);

		CommandLine += FString::Printf(TEXT(" -ResX=%d -ResY=%d"), WindowSize.X, WindowSize.Y);

		// If they request a size larger than their display, add -ForceRes to prevent the engine
		// from automatically resizing the new instance to fit within the bounds of the screen.
		if ((WindowSize.X <= 0 || WindowSize.X > DisplayMetrics.PrimaryDisplayWidth) || (WindowSize.Y <= 0 || WindowSize.Y > DisplayMetrics.PrimaryDisplayHeight))
		{
			CommandLine += TEXT(" -ForceRes");
		}
	}

	FString GameNameOrProjectFile;
	if (FPaths::IsProjectFilePathSet())
	{
		GameNameOrProjectFile = FString::Printf(TEXT("\"%s\""), *FPaths::GetProjectFilePath());
	}
	else
	{
		GameNameOrProjectFile = FApp::GetProjectName();
	}

	// Build the final command line
	FWorldContext & EditorContext = GetEditorWorldContext();
	FString MapName = EditorContext.World()->GetOutermost()->GetName();

	// Launch a new process.
	TMap<FString, FStringFormatArg> NamedArguments;
	NamedArguments.Add(TEXT("GameNameOrProjectFile"), GameNameOrProjectFile);
	if (NetMode != EPlayNetMode::PIE_Client)
	{
		// If we're not a client, build a PlayWorld URL to load to.
		FString ServerMapNameOverride;
		InParams.EditorPlaySettings->GetServerMapNameOverride(ServerMapNameOverride);
		
		// Allow the user to override which map the server should load.
		if (ServerMapNameOverride.Len() > 0)
		{
			UE_LOG(LogPlayLevel, Log, TEXT("Map Override specified in configuration, using %s instead of current map (%s)"), *ServerMapNameOverride, *MapName);
			MapName = ServerMapNameOverride;
		}

		NamedArguments.Add(TEXT("PlayWorldURL"), BuildPlayWorldURL(*MapName, false, UnrealURLParams));
	}
	else
	{
		// Otherwise hosts just connect and accept whatever the server's settings are.
		NamedArguments.Add(TEXT("PlayWorldURL"), UnrealURLParams);
	}
	NamedArguments.Add(TEXT("SubprocessCommandLine"), FCommandLine::GetSubprocessCommandline());
	NamedArguments.Add(TEXT("CommandLineParams"), CommandLine);

	FString FinalCommandLine = FString::Format(
		TEXT("{GameNameOrProjectFile} {PlayWorldURL} -game -PIEVIACONSOLE {SubprocessCommandLine} {CommandLineParams}"), NamedArguments);

	// Create a handle that we can keep track of for later killing.
	FPlayOnPCInfo& NewSession = PlayOnLocalPCSessions.Add_GetRef(FPlayOnPCInfo());
	const TCHAR* ExecutablePath = FPlatformProcess::ExecutablePath();
	uint32 ProcessID = 0;
	const bool bLaunchDetatched = true;
	const bool bLaunchMinimized = false;
	const bool bLaunchWindowHidden = false;
	const uint32 PriorityModifier = 0;
	NewSession.ProcessHandle = FPlatformProcess::CreateProc(
		FPlatformProcess::ExecutablePath(), *FinalCommandLine, bLaunchDetatched,
		bLaunchMinimized, bLaunchWindowHidden, &ProcessID,
		PriorityModifier, nullptr, nullptr, nullptr);

	if (!NewSession.ProcessHandle.IsValid())
	{
		UE_LOG(LogPlayLevel, Error, TEXT("Failed to run a copy of the game on this PC."));
	}

	// Notify anyone listening that we started a new Standalone Process.
	FEditorDelegates::BeginStandaloneLocalPlay.Broadcast(ProcessID);
}