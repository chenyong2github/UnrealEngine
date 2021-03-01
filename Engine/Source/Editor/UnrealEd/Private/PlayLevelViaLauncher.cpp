// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/EditorEngine.h"
#include "ITargetDeviceServicesModule.h"
#include "ILauncherServicesModule.h"
#include "EditorAnalytics.h"
#include "AnalyticsEventAttribute.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/CoreMisc.h"
#include "GameProjectGenerationModule.h"
#include "CookerSettings.h"
#include "UnrealEdMisc.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Settings/ProjectPackagingSettings.h"
#include "Framework/Notifications/NotificationManager.h"
#include "PlayLevel.h"
#include "Async/Async.h"
#include "Logging/MessageLog.h"
#include "TargetReceipt.h"
#include "DesktopPlatformModule.h"
#include "PlatformInfo.h"

#define LOCTEXT_NAMESPACE "PlayLevel"

static void HandleHyperlinkNavigate()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog"));
}

static void HandleCancelButtonClicked(ILauncherWorkerPtr LauncherWorker)
{
	if (LauncherWorker.IsValid())
	{
		LauncherWorker->Cancel();
	}
}

static void HandleOutputReceived(const FString& InMessage)
{
	if (InMessage.Contains(TEXT("Error:")))
	{
		UE_LOG(LogPlayLevel, Error, TEXT("%s"), *InMessage);
	}
	else if (InMessage.Contains(TEXT("Warning:")))
	{
		UE_LOG(LogPlayLevel, Warning, TEXT("%s"), *InMessage);
	}
	else
	{
		UE_LOG(LogPlayLevel, Log, TEXT("%s"), *InMessage);
	}
}

void UEditorEngine::StartPlayUsingLauncherSession(FRequestPlaySessionParams& InRequestParams)
{
	check(InRequestParams.SessionDestination == EPlaySessionDestinationType::Launcher);

	// Cache the DeviceId we've been asked to run on. This is used by the UI to know which device
	// clicking the button (without choosing from the dropdown) should use.
	LastPlayUsingLauncherDeviceId = InRequestParams.LauncherTargetDevice->DeviceId;
	LauncherSessionInfo = FLauncherCachedInfo();

	LauncherSessionInfo->PlayUsingLauncherDeviceName = PlaySessionRequest->LauncherTargetDevice->DeviceName;

	if (!ensureAlwaysMsgf(PlaySessionRequest->LauncherTargetDevice.IsSet(), TEXT("PlayUsingLauncher should not be called without a target device set!")))
	{
		CancelRequestPlaySession();
		return;
	}

	if (!ensureAlwaysMsgf(LastPlayUsingLauncherDeviceId.Len() > 0, TEXT("PlayUsingLauncher should not be called without a target device id set!")))
	{
		CancelRequestPlaySession();
		return;
	}

	ILauncherServicesModule& LauncherServicesModule = FModuleManager::LoadModuleChecked<ILauncherServicesModule>(TEXT("LauncherServices"));
	ITargetDeviceServicesModule& TargetDeviceServicesModule = FModuleManager::LoadModuleChecked<ITargetDeviceServicesModule>("TargetDeviceServices");

	//if the device is not authorized to be launched to, we need to pop an error instead of trying to launch
	FString LaunchPlatformName = LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@")));
	FString LaunchPlatformNameFromID = LastPlayUsingLauncherDeviceId.Right(LastPlayUsingLauncherDeviceId.Find(TEXT("@")));
	ITargetPlatform* LaunchPlatform = GetTargetPlatformManagerRef().FindTargetPlatform(LaunchPlatformName);

	// create a temporary device group and launcher profile
	ILauncherDeviceGroupRef DeviceGroup = LauncherServicesModule.CreateDeviceGroup(FGuid::NewGuid(), TEXT("PlayOnDevices"));
	if (LaunchPlatform != nullptr)
	{
		if (LaunchPlatformNameFromID.Equals(LaunchPlatformName))
		{
			// create a temporary list of devices for the target platform
			TArray<ITargetDevicePtr> TargetDevices;
			LaunchPlatform->GetAllDevices(TargetDevices);

			for (const ITargetDevicePtr& PlayDevice : TargetDevices)
			{
				// compose the device id
				FString PlayDeviceId = LaunchPlatformName + TEXT("@") + PlayDevice.Get()->GetId().GetDeviceName();
				if (PlayDevice.IsValid() && !PlayDevice->IsAuthorized())
				{
					CancelPlayUsingLauncher();
				}
				else
				{
					DeviceGroup->AddDevice(PlayDeviceId);
					UE_LOG(LogPlayLevel, Log, TEXT("Launcher Device ID: %s"), *PlayDeviceId);
				}
			}
		}
		else
		{
			ITargetDevicePtr PlayDevice = LaunchPlatform->GetDefaultDevice();
			if (PlayDevice.IsValid() && !PlayDevice->IsAuthorized())
			{
				CancelPlayUsingLauncher();
			}
			else
			{

				DeviceGroup->AddDevice(LastPlayUsingLauncherDeviceId);
				UE_LOG(LogPlayLevel, Log, TEXT("Launcher Device ID: %s"), *LastPlayUsingLauncherDeviceId);
			}
		}

		if (DeviceGroup.Get().GetNumDevices() == 0)
		{
			return;
		}
	}

	// set the build/launch configuration 
	EBuildConfiguration BuildConfiguration;
	const ULevelEditorPlaySettings* EditorPlaySettings = PlaySessionRequest->EditorPlaySettings;
	switch (EditorPlaySettings->LaunchConfiguration)
	{
	case LaunchConfig_Debug:
		BuildConfiguration = EBuildConfiguration::Debug;
		break;
	case LaunchConfig_Development:
		BuildConfiguration = EBuildConfiguration::Development;
		break;
	case LaunchConfig_Test:
		BuildConfiguration = EBuildConfiguration::Test;
		break;
	case LaunchConfig_Shipping:
		BuildConfiguration = EBuildConfiguration::Shipping;
		break;
	default:
		// same as the running editor
		BuildConfiguration = FApp::GetBuildConfiguration();
		break;
	}

	// does the project have any code?
	FGameProjectGenerationModule& GameProjectModule = FModuleManager::LoadModuleChecked<FGameProjectGenerationModule>(TEXT("GameProjectGeneration"));
	LauncherSessionInfo->bPlayUsingLauncherHasCode = GameProjectModule.Get().ProjectHasCodeFiles();

	// Figure out if we need to build anything
	ELauncherProfileBuildModes::Type BuildMode;
	if (EditorPlaySettings->BuildGameBeforeLaunch == EPlayOnBuildMode::PlayOnBuild_Always)
	{
		BuildMode = ELauncherProfileBuildModes::Build;
	}
	else if (EditorPlaySettings->BuildGameBeforeLaunch == EPlayOnBuildMode::PlayOnBuild_Never)
	{
		BuildMode = ELauncherProfileBuildModes::DoNotBuild;
	}
	else
	{
		BuildMode = ELauncherProfileBuildModes::Auto;
	}

	// Assume it's building unless disabled
	LauncherSessionInfo->bPlayUsingLauncherBuild = (BuildMode != ELauncherProfileBuildModes::DoNotBuild);

	// Setup launch profile, keep the setting here to a minimum.
	ILauncherProfileRef LauncherProfile = LauncherServicesModule.CreateProfile(TEXT("Launch On Device"));
	LauncherProfile->SetBuildMode(BuildMode);
	LauncherProfile->SetBuildConfiguration(BuildConfiguration);

	// select the quickest cook mode based on which in editor cook mode is enabled
	bool bIncrimentalCooking = true;
	LauncherProfile->AddCookedPlatform(LaunchPlatformName);
	ELauncherProfileCookModes::Type CurrentLauncherCookMode = ELauncherProfileCookModes::ByTheBook;
	bool bCanCookByTheBookInEditor = true;
	bool bCanCookOnTheFlyInEditor = true;
	for (const FString& PlatformName : LauncherProfile->GetCookedPlatforms())
	{
		if (CanCookByTheBookInEditor(PlatformName) == false)
		{
			bCanCookByTheBookInEditor = false;
		}
		if (CanCookOnTheFlyInEditor(PlatformName) == false)
		{
			bCanCookOnTheFlyInEditor = false;
		}
	}
	if (bCanCookByTheBookInEditor)
	{
		CurrentLauncherCookMode = ELauncherProfileCookModes::ByTheBookInEditor;
	}
	if (bCanCookOnTheFlyInEditor)
	{
		CurrentLauncherCookMode = ELauncherProfileCookModes::OnTheFlyInEditor;
		bIncrimentalCooking = false;
	}
	if (GetDefault<UCookerSettings>()->bCookOnTheFlyForLaunchOn)
	{
		CurrentLauncherCookMode = ELauncherProfileCookModes::OnTheFly;
		bIncrimentalCooking = false;
	}
	LauncherProfile->SetCookMode(CurrentLauncherCookMode);
	LauncherProfile->SetUnversionedCooking(!bIncrimentalCooking);
	LauncherProfile->SetIncrementalCooking(bIncrimentalCooking);
	LauncherProfile->SetDeployedDeviceGroup(DeviceGroup);
	LauncherProfile->SetIncrementalDeploying(bIncrimentalCooking);
	LauncherProfile->SetEditorExe(FUnrealEdMisc::Get().GetExecutableForCommandlets());

	const FString DummyIOSDeviceName(FString::Printf(TEXT("All_iOS_On_%s"), FPlatformProcess::ComputerName()));
	const FString DummyTVOSDeviceName(FString::Printf(TEXT("All_tvOS_On_%s"), FPlatformProcess::ComputerName()));

	if ((LaunchPlatformName != TEXT("IOS") && LaunchPlatformName != TEXT("TVOS")) ||
		(!LauncherSessionInfo->PlayUsingLauncherDeviceName.Contains(DummyIOSDeviceName) && !LauncherSessionInfo->PlayUsingLauncherDeviceName.Contains(DummyTVOSDeviceName)))
	{
		LauncherProfile->SetLaunchMode(ELauncherProfileLaunchModes::DefaultRole);
	}

	if (LauncherProfile->GetCookMode() == ELauncherProfileCookModes::OnTheFlyInEditor || LauncherProfile->GetCookMode() == ELauncherProfileCookModes::OnTheFly)
	{
		LauncherProfile->SetDeploymentMode(ELauncherProfileDeploymentModes::FileServer);
	}

	switch(EditorPlaySettings->PackFilesForLaunch)
	{
	default:
	case EPlayOnPakFileMode::NoPak:
		break;
	case EPlayOnPakFileMode::PakNoCompress:
		LauncherProfile->SetCompressed( false );
		LauncherProfile->SetDeployWithUnrealPak( true );
		break;
	case EPlayOnPakFileMode::PakCompress:
		LauncherProfile->SetCompressed( true );
		LauncherProfile->SetDeployWithUnrealPak( true );
		break;
	}


	TArray<UBlueprint*> ErroredBlueprints;
	FInternalPlayLevelUtils::ResolveDirtyBlueprints(!EditorPlaySettings->bAutoCompileBlueprintsOnLaunch, ErroredBlueprints, false);

	TArray<FString> MapNames;
	FWorldContext & EditorContext = GetEditorWorldContext();

	// Load maps in place as we saved them above
	FString EditorMapName = EditorContext.World()->GetOutermost()->GetName();
	MapNames.Add(EditorMapName);

	FString InitialMapName;
	if (MapNames.Num() > 0)
	{
		InitialMapName = MapNames[0];
	}

	LauncherProfile->GetDefaultLaunchRole()->SetInitialMap(InitialMapName);

	for (const FString& MapName : MapNames)
	{
		LauncherProfile->AddCookedMap(MapName);
	}

	if (LauncherProfile->GetCookMode() == ELauncherProfileCookModes::ByTheBookInEditor)
	{
		TArray<ITargetPlatform*> TargetPlatforms;
		for (const FString& PlatformName : LauncherProfile->GetCookedPlatforms())
		{
			ITargetPlatform* TargetPlatform = GetTargetPlatformManager()->FindTargetPlatform(PlatformName);
			// todo pass in all the target platforms instead of just the single platform
			// crashes if two requests are inflight but we can support having multiple platforms cooking at once
			TargetPlatforms.Add(TargetPlatform);
		}
		const TArray<FString> &CookedMaps = LauncherProfile->GetCookedMaps();

		// const TArray<FString>& CookedMaps = ChainState.Profile->GetCookedMaps();
		TArray<FString> CookDirectories;
		TArray<FString> IniMapSections;

		StartCookByTheBookInEditor(TargetPlatforms, CookedMaps, CookDirectories, GetDefault<UProjectPackagingSettings>()->CulturesToStage, IniMapSections);

		FIsCookFinishedDelegate &CookerFinishedDelegate = LauncherProfile->OnIsCookFinished();

		CookerFinishedDelegate.BindUObject(this, &UEditorEngine::IsCookByTheBookInEditorFinished);

		FCookCanceledDelegate &CookCancelledDelegate = LauncherProfile->OnCookCanceled();

		CookCancelledDelegate.BindUObject(this, &UEditorEngine::CancelCookByTheBookInEditor);
	}

	ILauncherPtr Launcher = LauncherServicesModule.CreateLauncher();
	GEditor->LauncherWorker = Launcher->Launch(TargetDeviceServicesModule.GetDeviceProxyManager(), LauncherProfile);

	// create notification item
	FText LaunchingText = LOCTEXT("LauncherTaskInProgressNotificationNoDevice", "Launching...");
	FNotificationInfo Info(LaunchingText);

	Info.Image = FEditorStyle::GetBrush(TEXT("MainFrame.CookContent"));
	Info.bFireAndForget = false;
	Info.ExpireDuration = 10.0f;
	Info.Hyperlink = FSimpleDelegate::CreateStatic(HandleHyperlinkNavigate);
	Info.HyperlinkText = LOCTEXT("ShowOutputLogHyperlink", "Show Output Log");
	Info.ButtonDetails.Add(
		FNotificationButtonInfo(
			LOCTEXT("LauncherTaskCancel", "Cancel"),
			LOCTEXT("LauncherTaskCancelToolTip", "Cancels execution of this task."),
			FSimpleDelegate::CreateStatic(HandleCancelButtonClicked, GEditor->LauncherWorker)
		)
	);

	// Launch doesn't block PIE/Compile requests as it's an async background process, so we just
	// cancel the request to denote it as having been handled. This has to come after we've used
	// anything we might need from the original request.
	CancelRequestPlaySession();

	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

	if (!NotificationItem.IsValid())
	{
		return;
	}

	// analytics for launch on
	int32 ErrorCode = 0;
	FEditorAnalytics::ReportEvent(TEXT("Editor.LaunchOn.Started"), LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@"))), LauncherSessionInfo->bPlayUsingLauncherHasCode);

	NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);

	TWeakPtr<SNotificationItem> NotificationItemPtr(NotificationItem);
	if (GEditor->LauncherWorker.IsValid() && GEditor->LauncherWorker->GetStatus() != ELauncherWorkerStatus::Completed)
	{
		GEditor->PlayEditorSound(TEXT("/Engine/EditorSounds/Notifications/CompileStart_Cue.CompileStart_Cue"));
		GEditor->LauncherWorker->OnOutputReceived().AddStatic(HandleOutputReceived);
		GEditor->LauncherWorker->OnStageStarted().AddUObject(this, &UEditorEngine::HandleStageStarted, NotificationItemPtr);
		GEditor->LauncherWorker->OnStageCompleted().AddUObject(this, &UEditorEngine::HandleStageCompleted, LauncherSessionInfo->bPlayUsingLauncherHasCode, NotificationItemPtr);
		GEditor->LauncherWorker->OnCompleted().AddUObject(this, &UEditorEngine::HandleLaunchCompleted, LauncherSessionInfo->bPlayUsingLauncherHasCode, NotificationItemPtr);
		GEditor->LauncherWorker->OnCanceled().AddUObject(this, &UEditorEngine::HandleLaunchCanceled, LauncherSessionInfo->bPlayUsingLauncherHasCode, NotificationItemPtr);
	}
	else
	{
		GEditor->LauncherWorker.Reset();
		GEditor->PlayEditorSound(TEXT("/Engine/EditorSounds/Notifications/CompileFailed_Cue.CompileFailed_Cue"));

		NotificationItem->SetText(LOCTEXT("LauncherTaskFailedNotification", "Failed to launch task!"));
		NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		NotificationItem->ExpireAndFadeout();
		
		// analytics for launch on
		TArray<FAnalyticsEventAttribute> ParamArray;
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("Time"), 0.0));
		FEditorAnalytics::ReportEvent(TEXT("Editor.LaunchOn.Failed"), LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@"))), LauncherSessionInfo->bPlayUsingLauncherHasCode, EAnalyticsErrorCodes::LauncherFailed, ParamArray);
		
		LauncherSessionInfo.Reset();
	}
}

void UEditorEngine::CancelPlayingViaLauncher()
{
	if (LauncherWorker.IsValid())
	{
		LauncherWorker->CancelAndWait();
	}
}

// Deprecated, just formats a RequestPlaySession instead.
void UEditorEngine::AutomationPlayUsingLauncher(const FString& InLauncherDeviceId)
{
	FRequestPlaySessionParams::FLauncherDeviceInfo LaunchedDeviceInfo;
	LaunchedDeviceInfo.DeviceId = InLauncherDeviceId;
	LaunchedDeviceInfo.DeviceName = InLauncherDeviceId.Right(InLauncherDeviceId.Find(TEXT("@")));

	FRequestPlaySessionParams Params;
	Params.LauncherTargetDevice = LaunchedDeviceInfo;

	RequestPlaySession(Params);

	// Immediately start our requested play session
	StartQueuedPlaySessionRequest();
}

/** 
* Cancel Play using Launcher on error 
* 
* if the physical device is not authorized to be launched to, we need to pop an error instead of trying to launch
*/
void UEditorEngine::CancelPlayUsingLauncher()
{
	FText LaunchingText = LOCTEXT("LauncherTaskInProgressNotificationNotAuthorized", "Cannot launch to this device until this computer is authorized from the device");
	FNotificationInfo Info(LaunchingText);
	Info.ExpireDuration = 5.0f;
	TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(SNotificationItem::CS_Fail);
		Notification->ExpireAndFadeout();
	}
}

/* FMainFrameActionCallbacks callbacks
 *****************************************************************************/

class FLauncherNotificationTask
{
public:

	FLauncherNotificationTask( TWeakPtr<SNotificationItem> InNotificationItemPtr, SNotificationItem::ECompletionState InCompletionState, const FText& InText )
		: CompletionState(InCompletionState)
		, NotificationItemPtr(InNotificationItemPtr)
		, Text(InText)
	{ }

	void DoTask( ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent )
	{
		if (NotificationItemPtr.IsValid())
		{
			if (CompletionState == SNotificationItem::CS_Fail)
			{
				GEditor->PlayEditorSound(TEXT("/Engine/EditorSounds/Notifications/CompileFailed_Cue.CompileFailed_Cue"));
			}
			else if (CompletionState == SNotificationItem::CS_Success)
			{
				GEditor->PlayEditorSound(TEXT("/Engine/EditorSounds/Notifications/CompileSuccess_Cue.CompileSuccess_Cue"));
			}

			TSharedPtr<SNotificationItem> NotificationItem = NotificationItemPtr.Pin();
			NotificationItem->SetText(Text);
			NotificationItem->SetCompletionState(CompletionState);
			if (CompletionState == SNotificationItem::CS_Success || CompletionState == SNotificationItem::CS_Fail)
			{
				NotificationItem->ExpireAndFadeout();
			}
		}
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }
	ENamedThreads::Type GetDesiredThread( ) { return ENamedThreads::GameThread; }
	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FLauncherNotificationTask, STATGROUP_TaskGraphTasks);
	}

private:

	SNotificationItem::ECompletionState CompletionState;
	TWeakPtr<SNotificationItem> NotificationItemPtr;
	FText Text;
};


void UEditorEngine::HandleStageStarted(const FString& InStage, TWeakPtr<SNotificationItem> NotificationItemPtr)
{
	if (!LauncherSessionInfo.IsSet())
	{
		UE_LOG(LogPlayLevel, Warning, TEXT("HandleStageStarted called for Stage: %s but the session was canceled, ignoring."), *InStage);
		return;
	}

	bool bSetNotification = true;
	FFormatNamedArguments Arguments;
	FText NotificationText;
	if (InStage.Contains(TEXT("Cooking")) || InStage.Contains(TEXT("Cook Task")))
	{
		FString PlatformName = LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@")));
		if (PlatformName.Contains(TEXT("NoEditor")))
		{
			PlatformName = PlatformName.Left(PlatformName.Find(TEXT("NoEditor")));
		}
		Arguments.Add(TEXT("PlatformName"), FText::FromString(PlatformName));
		NotificationText = FText::Format(LOCTEXT("LauncherTaskProcessingNotification", "Processing Assets for {PlatformName}..."), Arguments);
	}
	else if (InStage.Contains(TEXT("Build Task")))
	{
		FString PlatformName = LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@")));
		if (PlatformName.Contains(TEXT("NoEditor")))
		{
			PlatformName = PlatformName.Left(PlatformName.Find(TEXT("NoEditor")));
		}
		Arguments.Add(TEXT("PlatformName"), FText::FromString(PlatformName));
		if (!LauncherSessionInfo->bPlayUsingLauncherBuild)
		{
			NotificationText = FText::Format(LOCTEXT("LauncherTaskValidateNotification", "Validating Executable for {PlatformName}..."), Arguments);
		}
		else
		{
			NotificationText = FText::Format(LOCTEXT("LauncherTaskBuildNotification", "Building Executable for {PlatformName}..."), Arguments);
		}
	}
	else if (InStage.Contains(TEXT("Deploy Task")))
	{
		Arguments.Add(TEXT("DeviceName"), FText::FromString(LauncherSessionInfo->PlayUsingLauncherDeviceName));
		if (LauncherSessionInfo->PlayUsingLauncherDeviceName.Len() == 0)
		{
			NotificationText = FText::Format(LOCTEXT("LauncherTaskStageNotificationNoDevice", "Deploying Executable and Assets..."), Arguments);
		}
		else
		{
			NotificationText = FText::Format(LOCTEXT("LauncherTaskStageNotification", "Deploying Executable and Assets to {DeviceName}..."), Arguments);
		}
	}
	else if (InStage.Contains(TEXT("Run Task")))
	{
		Arguments.Add(TEXT("GameName"), FText::FromString(FApp::GetProjectName()));
		Arguments.Add(TEXT("DeviceName"), FText::FromString(LauncherSessionInfo->PlayUsingLauncherDeviceName));
		if (LauncherSessionInfo->PlayUsingLauncherDeviceName.Len() == 0)
		{
			NotificationText = FText::Format(LOCTEXT("LauncherTaskRunNotificationNoDevice", "Running {GameName}..."), Arguments);
		}
		else
		{
			NotificationText = FText::Format(LOCTEXT("LauncherTaskRunNotification", "Running {GameName} on {DeviceName}..."), Arguments);
		}
	}
	else
	{
		bSetNotification = false;
	}

	if (bSetNotification)
	{
		TGraphTask<FLauncherNotificationTask>::CreateTask().ConstructAndDispatchWhenReady(
			NotificationItemPtr,
			SNotificationItem::CS_Pending,
			NotificationText
		);
	}
}

void UEditorEngine::HandleStageCompleted(const FString& InStage, double StageTime, bool bHasCode, TWeakPtr<SNotificationItem> NotificationItemPtr)
{
	UE_LOG(LogPlayLevel, Log, TEXT("Completed Launch On Stage: %s, Time: %f"), *InStage, StageTime);

	// analytics for launch on
	TArray<FAnalyticsEventAttribute> ParamArray;
	ParamArray.Add(FAnalyticsEventAttribute(TEXT("Time"), StageTime));
	ParamArray.Add(FAnalyticsEventAttribute(TEXT("StageName"), InStage));
	FEditorAnalytics::ReportEvent(TEXT( "Editor.LaunchOn.StageComplete" ), LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@"))), bHasCode, ParamArray);
}

void UEditorEngine::HandleLaunchCanceled(double TotalTime, bool bHasCode, TWeakPtr<SNotificationItem> NotificationItemPtr)
{
	TGraphTask<FLauncherNotificationTask>::CreateTask().ConstructAndDispatchWhenReady(
		NotificationItemPtr,
		SNotificationItem::CS_Fail,
		LOCTEXT("LaunchtaskFailedNotification", "Launch canceled!")
	);

	// analytics for launch on
	TArray<FAnalyticsEventAttribute> ParamArray;
	ParamArray.Add(FAnalyticsEventAttribute(TEXT("Time"), TotalTime));
	FEditorAnalytics::ReportEvent(TEXT( "Editor.LaunchOn.Canceled" ), LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@"))), bHasCode, ParamArray);

	LauncherSessionInfo.Reset();
}

void UEditorEngine::HandleLaunchCompleted(bool Succeeded, double TotalTime, int32 ErrorCode, bool bHasCode, TWeakPtr<SNotificationItem> NotificationItemPtr)
{
	const FString DummyIOSDeviceName(FString::Printf(TEXT("All_iOS_On_%s"), FPlatformProcess::ComputerName()));
	const FString DummyTVOSDeviceName(FString::Printf(TEXT("All_tvOS_On_%s"), FPlatformProcess::ComputerName()));
	if (Succeeded)
	{
		FText CompletionMsg;
		if ((LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@"))) == TEXT("IOS") && LauncherSessionInfo->PlayUsingLauncherDeviceName.Contains(DummyIOSDeviceName)) ||
			(LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@"))) == TEXT("TVOS") && LauncherSessionInfo->PlayUsingLauncherDeviceName.Contains(DummyTVOSDeviceName)))
		{
			CompletionMsg = LOCTEXT("DeploymentTaskCompleted", "Deployment complete! Open the app on your device to launch.");
		}
		else
		{
			CompletionMsg = LOCTEXT("LauncherTaskCompleted", "Launch complete!!");
		}

		TGraphTask<FLauncherNotificationTask>::CreateTask().ConstructAndDispatchWhenReady(
			NotificationItemPtr,
			SNotificationItem::CS_Success,
			CompletionMsg
		);

		// analytics for launch on
		TArray<FAnalyticsEventAttribute> ParamArray;
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("Time"), TotalTime));
		FEditorAnalytics::ReportEvent(TEXT( "Editor.LaunchOn.Completed" ), LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@"))), bHasCode, ParamArray);

		UE_LOG(LogPlayLevel, Log, TEXT("Launch On Completed. Time: %f"), TotalTime);
	}
	else
	{
		FText CompletionMsg;
		if ((LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@"))) == TEXT("IOS") && LauncherSessionInfo->PlayUsingLauncherDeviceName.Contains(DummyIOSDeviceName)) ||
			(LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@"))) == TEXT("TVOS") && LauncherSessionInfo->PlayUsingLauncherDeviceName.Contains(DummyTVOSDeviceName)))
		{
			CompletionMsg = LOCTEXT("DeploymentTaskFailed", "Deployment failed!");
		}
		else
		{
			CompletionMsg = LOCTEXT("LauncherTaskFailed", "Launch failed!");
		}
		
		AsyncTask(ENamedThreads::GameThread, [=]
		{
			FMessageLog MessageLog("PackagingResults");

			MessageLog.Error()
				->AddToken(FTextToken::Create(CompletionMsg))
				->AddToken(FTextToken::Create(FText::FromString(FEditorAnalytics::TranslateErrorCode(ErrorCode))));

			// flush log, because it won't be destroyed until the notification popup closes
			MessageLog.NumMessages(EMessageSeverity::Info);
		});

		TGraphTask<FLauncherNotificationTask>::CreateTask().ConstructAndDispatchWhenReady(
			NotificationItemPtr,
			SNotificationItem::CS_Fail,
			CompletionMsg
		);

		TArray<FAnalyticsEventAttribute> ParamArray;
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("Time"), TotalTime));
		FEditorAnalytics::ReportEvent(TEXT( "Editor.LaunchOn.Failed" ), LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@"))), bHasCode, ErrorCode, ParamArray);
	}

	LauncherSessionInfo.Reset();
}

FString UEditorEngine::GetPlayOnTargetPlatformName() const
{
	return LastPlayUsingLauncherDeviceId.Left(LastPlayUsingLauncherDeviceId.Find(TEXT("@")));
}

void UEditorEngine::PlayUsingLauncher()
{
	// Deprecated, just a wrapper around RequestPlaySession now.
	FRequestPlaySessionParams::FLauncherDeviceInfo DeviceInfo;
	DeviceInfo.DeviceId = LastPlayUsingLauncherDeviceId;

	FRequestPlaySessionParams Params;
	Params.LauncherTargetDevice = DeviceInfo;
	
	RequestPlaySession(Params);
}

#undef LOCTEXT_NAMESPACE // "PlayLevel"
