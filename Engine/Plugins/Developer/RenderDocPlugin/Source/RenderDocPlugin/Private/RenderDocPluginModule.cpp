// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderDocPluginModule.h"

#include "Internationalization/Internationalization.h"
#include "RendererInterface.h"
#include "RenderingThread.h"

#include "RenderDocPluginNotification.h"
#include "RenderDocPluginSettings.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/GameViewportClient.h"
#include "RenderCaptureInterface.h"
#include "Misc/AutomationTest.h"

#if WITH_EDITOR
#include "UnrealClient.h"
#include "Editor/EditorEngine.h"
extern UNREALED_API UEditorEngine* GEditor;
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Editor.h"
#endif

DEFINE_LOG_CATEGORY(RenderDocPlugin);

#define LOCTEXT_NAMESPACE "RenderDocPlugin"

static TAutoConsoleVariable<int32> CVarRenderDocCaptureAllActivity(
	TEXT("renderdoc.CaptureAllActivity"),
	0,
	TEXT("0 - RenderDoc will only capture data from the current viewport. ")
	TEXT("1 - RenderDoc will capture all activity, in all viewports and editor windows for the entire frame."));
static TAutoConsoleVariable<int32> CVarRenderDocCaptureCallstacks(
	TEXT("renderdoc.CaptureCallstacks"),
	1,
	TEXT("0 - Callstacks will not be captured by RenderDoc. ")
	TEXT("1 - Capture callstacks for each API call."));
static TAutoConsoleVariable<int32> CVarRenderDocReferenceAllResources(
	TEXT("renderdoc.ReferenceAllResources"),
	0,
	TEXT("0 - Only include resources that are actually used. ")
	TEXT("1 - Include all rendering resources in the capture, even those that have not been used during the frame. ")
	TEXT("Please note that doing this will significantly increase capture size."));
static TAutoConsoleVariable<int32> CVarRenderDocSaveAllInitials(
	TEXT("renderdoc.SaveAllInitials"),
	0,
	TEXT("0 - Disregard initial states of resources. ")
	TEXT("1 - Always capture the initial state of all rendering resources. ")
	TEXT("Please note that doing this will significantly increase capture size."));
static TAutoConsoleVariable<int32> CVarRenderDocCaptureDelayInSeconds(
	TEXT("renderdoc.CaptureDelayInSeconds"),
	1,
	TEXT("0 - Capture delay's unit is in frames.")
	TEXT("1 - Capture delay's unit is in seconds."));
static TAutoConsoleVariable<int32> CVarRenderDocCaptureDelay(
	TEXT("renderdoc.CaptureDelay"),
	0,
	TEXT("If > 0, RenderDoc will trigger the capture only after this amount of time (or frames, if CaptureDelayInSeconds is false) has passed."));
static TAutoConsoleVariable<int32> CVarRenderDocCaptureFrameCount(
	TEXT("renderdoc.CaptureFrameCount"),
	0,
	TEXT("If > 0, the RenderDoc capture will encompass more than a single frame. Note: this implies that all activity in all viewports and editor windows will be captured (i.e. same as CaptureAllActivity)"));

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper classes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FRenderDocAsyncGraphTask : public FAsyncGraphTaskBase
{
	ENamedThreads::Type TargetThread;
	TFunction<void()> TheTask;

	FRenderDocAsyncGraphTask(ENamedThreads::Type Thread, TFunction<void()>&& Task) : TargetThread(Thread), TheTask(MoveTemp(Task)) { }
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) { TheTask(); }
	ENamedThreads::Type GetDesiredThread() { return(TargetThread); }
};

class FRenderDocFrameCapturer
{
public:
	static RENDERDOC_DevicePointer GetRenderdocDevicePointer()
	{
		RENDERDOC_DevicePointer Device;
		if(0 == FCString::Strcmp(GDynamicRHI->GetName(),TEXT("Vulkan")))
		{
			Device = GDynamicRHI->RHIGetNativeInstance();
#ifndef RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE
#define RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(inst) (*((void **)(inst)))
#endif
			Device = RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(Device);
		}
		else
		{
			Device = GDynamicRHI->RHIGetNativeDevice();
		}
		return Device;

	}

	static void BeginCapture(HWND WindowHandle, FRenderDocPluginLoader::RENDERDOC_API_CONTEXT* RenderDocAPI, FRenderDocPluginModule* Plugin)
	{
		UE4_GEmitDrawEvents_BeforeCapture = GetEmitDrawEvents();
		SetEmitDrawEvents(true);
		RenderDocAPI->StartFrameCapture(GetRenderdocDevicePointer(), WindowHandle);
	}

	static void EndCapture(HWND WindowHandle, FRenderDocPluginLoader::RENDERDOC_API_CONTEXT* RenderDocAPI, FRenderDocPluginModule* Plugin, const FString& DestPath, FRenderDocPluginModule::ELaunchAfterCapture LaunchOption)
	{
		FRHICommandListExecutor::GetImmediateCommandList().SubmitCommandsAndFlushGPU();
		RenderDocAPI->EndFrameCapture(GetRenderdocDevicePointer(), WindowHandle);

		SetEmitDrawEvents(UE4_GEmitDrawEvents_BeforeCapture);

		bool bLaunchDestPath = false;
		if (!DestPath.IsEmpty())
		{
			IPlatformFile& PlatformFileSystem = IPlatformFile::GetPlatformPhysical();

			FString NewestCapturePath = Plugin->GetNewestCapture();
			UE_LOG(RenderDocPlugin, Log, TEXT("Copying: %s to %s"), *NewestCapturePath, *DestPath);

			if (PlatformFileSystem.FileExists(*DestPath))
			{
				PlatformFileSystem.DeleteFile(*DestPath);
			}

			if (PlatformFileSystem.CreateDirectoryTree(*FPaths::GetPath(DestPath)))
			{
				if (PlatformFileSystem.MoveFile(*DestPath, *NewestCapturePath))
				{
					bLaunchDestPath = true;
				}
				else
				{
					uint32 WriteErrorCode = FPlatformMisc::GetLastError();
					TCHAR WriteErrorBuffer[2048];
					FPlatformMisc::GetSystemErrorMessage(WriteErrorBuffer, 2048, WriteErrorCode);
					UE_LOG(RenderDocPlugin, Warning, TEXT("Failed to move: %s to %s. WriteError: %u (%s)"), *NewestCapturePath, *DestPath, WriteErrorCode, WriteErrorBuffer);
				}
			}
			else
			{
				uint32 WriteErrorCode = FPlatformMisc::GetLastError();
				TCHAR WriteErrorBuffer[2048];
				FPlatformMisc::GetSystemErrorMessage(WriteErrorBuffer, 2048, WriteErrorCode);
				UE_LOG(RenderDocPlugin, Warning, TEXT("Failed to create directory tree for: %s. WriteError: %u (%s)"), *DestPath, WriteErrorCode, WriteErrorBuffer);
			}
		}

		if (LaunchOption == FRenderDocPluginModule::ELaunchAfterCapture::Yes)
		{	
			TGraphTask<FRenderDocAsyncGraphTask>::CreateTask().ConstructAndDispatchWhenReady(ENamedThreads::GameThread, [Plugin, DestPath, bLaunchDestPath]()
			{
				if (bLaunchDestPath)
				{
					Plugin->StartRenderDoc(DestPath);
				}
				else
				{
					Plugin->StartRenderDoc(FString());
				}
			});
		}
	}

private:
	static bool UE4_GEmitDrawEvents_BeforeCapture;
};
bool FRenderDocFrameCapturer::UE4_GEmitDrawEvents_BeforeCapture = false;

class FRenderDocDummyInputDevice : public IInputDevice
{
public:
	FRenderDocDummyInputDevice(FRenderDocPluginModule* InPlugin) : ThePlugin(InPlugin) { }
	virtual ~FRenderDocDummyInputDevice() { }

	/** Tick the interface (used for controlling full engine frame captures). */
	virtual void Tick(float DeltaTime) override
	{
		check(ThePlugin);
		ThePlugin->Tick(DeltaTime);
	}

	/** The remaining interfaces are irrelevant for this dummy input device. */
	virtual void SendControllerEvents() override { }
	virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override { }
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override { return(false); }
	virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override { }
	virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues &values) override { }

private:
	FRenderDocPluginModule* ThePlugin;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FRenderDocPluginModule
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<class IInputDevice> FRenderDocPluginModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	UE_LOG(RenderDocPlugin, Log, TEXT("Creating dummy input device (for intercepting engine ticks)"));
	FRenderDocDummyInputDevice* InputDev = new FRenderDocDummyInputDevice(this);
	return MakeShareable((IInputDevice*)InputDev);
}

void FRenderDocPluginModule::StartupModule()
{
#if WITH_EDITOR && !UE_BUILD_SHIPPING // Disable in shipping builds
	Loader.Initialize();
	RenderDocAPI = nullptr;

#if WITH_EDITOR
	EditorExtensions = nullptr;
#endif

	if (Loader.RenderDocAPI == nullptr)
	{
		return;
	}

	InjectDebugExecKeybind();

	// Regrettably, GUsingNullRHI is set to true AFTER the PostConfigInit modules
	// have been loaded (RenderDoc plugin being one of them). When this code runs
	// the following condition will never be true, so it must be tested again in
	// the Toolbar initialization code.
	if (GUsingNullRHI)
	{
		UE_LOG(RenderDocPlugin, Warning, TEXT("RenderDoc Plugin will not be loaded because a Null RHI (Cook Server, perhaps) is being used."));
		return;
	}

	// Obtain a handle to the RenderDoc DLL that has been loaded by the RenderDoc
	// Loader Plugin; no need for error handling here since the Loader would have
	// already handled and logged these errors (but check() them just in case...)
	RenderDocAPI = Loader.RenderDocAPI;
	check(RenderDocAPI);

	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
	DelayedCaptureTick = 0;
	DelayedCaptureSeconds = 0.0;
	CaptureFrameCount = 0;
	CaptureEndTick = 0;
	bCaptureDelayInSeconds = false;
	bPendingCapture = false;
	bCaptureInProgress = false;
	bShouldCaptureAllActivity = false;

	// Setup RenderDoc settings
	FString RenderDocCapturePath = FPaths::ProjectSavedDir() / TEXT("RenderDocCaptures");
	if (!IFileManager::Get().DirectoryExists(*RenderDocCapturePath))
	{
		IFileManager::Get().MakeDirectory(*RenderDocCapturePath, true);
	}

	FString CapturePath = FPaths::Combine(*RenderDocCapturePath, *FDateTime::Now().ToString());
	CapturePath = FPaths::ConvertRelativePathToFull(CapturePath);
	FPaths::NormalizeDirectoryName(CapturePath);
	
	RenderDocAPI->SetLogFilePathTemplate(TCHAR_TO_ANSI(*CapturePath));

	RenderDocAPI->SetFocusToggleKeys(nullptr, 0);
	RenderDocAPI->SetCaptureKeys(nullptr, 0);

	RenderDocAPI->SetCaptureOptionU32(eRENDERDOC_Option_CaptureCallstacks, CVarRenderDocCaptureCallstacks.GetValueOnAnyThread() ? 1 : 0);
	RenderDocAPI->SetCaptureOptionU32(eRENDERDOC_Option_RefAllResources, CVarRenderDocReferenceAllResources.GetValueOnAnyThread() ? 1 : 0);
	RenderDocAPI->SetCaptureOptionU32(eRENDERDOC_Option_SaveAllInitials, CVarRenderDocSaveAllInitials.GetValueOnAnyThread() ? 1 : 0);

	RenderDocAPI->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);

#if WITH_EDITOR
	EditorExtensions = new FRenderDocPluginEditorExtension(this);
#endif

	static FAutoConsoleCommand CCmdRenderDocCaptureFrame = FAutoConsoleCommand(
		TEXT("renderdoc.CaptureFrame"),
		TEXT("Captures the rendering commands of the next frame and launches RenderDoc"),
		FConsoleCommandDelegate::CreateRaw(this, &FRenderDocPluginModule::CaptureFrame)
	);

	static FAutoConsoleCommand CCmdRenderDocCapturePIE = FAutoConsoleCommand(
		TEXT("renderdoc.CapturePIE"),
		TEXT("Starts a PIE session and captures the specified number of frames from the start."),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FRenderDocPluginModule::CapturePIE)
	);

	BindCaptureCallbacks();

	UE_LOG(RenderDocPlugin, Log, TEXT("RenderDoc plugin is ready!"));
#endif
}

void FRenderDocPluginModule::BeginCapture()
{
	UE_LOG(RenderDocPlugin, Log, TEXT("Capture frame and launch renderdoc!"));
	ShowNotification(LOCTEXT("RenderDocBeginCaptureNotification", "RenderDoc capture started"), true);

	pRENDERDOC_SetCaptureOptionU32 SetOptions = Loader.RenderDocAPI->SetCaptureOptionU32;
	int ok = SetOptions(eRENDERDOC_Option_CaptureCallstacks, CVarRenderDocCaptureCallstacks.GetValueOnAnyThread() ? 1 : 0); check(ok);
		ok = SetOptions(eRENDERDOC_Option_RefAllResources, CVarRenderDocReferenceAllResources.GetValueOnAnyThread() ? 1 : 0); check(ok);
		ok = SetOptions(eRENDERDOC_Option_SaveAllInitials, CVarRenderDocSaveAllInitials.GetValueOnAnyThread() ? 1 : 0); check(ok);

	HWND WindowHandle = GetActiveWindow();

	typedef FRenderDocPluginLoader::RENDERDOC_API_CONTEXT RENDERDOC_API_CONTEXT;
	FRenderDocPluginModule* Plugin = this;
	FRenderDocPluginLoader::RENDERDOC_API_CONTEXT* RenderDocAPILocal = RenderDocAPI;
	ENQUEUE_RENDER_COMMAND(StartRenderDocCapture)(
		[Plugin, WindowHandle, RenderDocAPILocal](FRHICommandListImmediate& RHICmdList)
		{
			FRenderDocFrameCapturer::BeginCapture(WindowHandle, RenderDocAPILocal, Plugin);
		});
}

bool FRenderDocPluginModule::ShouldCaptureAllActivity() const
{
	// capturing more than 1 frame means that we can't just capture the current viewport : 
	return CVarRenderDocCaptureAllActivity.GetValueOnAnyThread() || (CVarRenderDocCaptureFrameCount.GetValueOnAnyThread() > 1);
}

void FRenderDocPluginModule::ShowNotification(const FText& Message, bool bForceNewNotification)
{
#if WITH_EDITOR
	FRenderDocPluginNotification::Get().ShowNotification(Message, bForceNewNotification);
#else
	GEngine->AddOnScreenDebugMessage((uint64)-1, 2.0f, FColor::Emerald, Message.ToString());
#endif
}

void FRenderDocPluginModule::InjectDebugExecKeybind()
{
	// Inject our key bind into the debug execs
	FConfigFile* ConfigFile = nullptr;
	// Look for the first matching INI file entry
	for (TMap<FString, FConfigFile>::TIterator It(*GConfig); It; ++It)
	{
		if (It.Key().EndsWith(TEXT("Input.ini")))
		{
			ConfigFile = &It.Value();
			break;
		}
	}
	check(ConfigFile != nullptr);
	FConfigSection* Section = ConfigFile->Find(TEXT("/Script/Engine.PlayerInput"));
	if (Section != nullptr)
	{
		Section->HandleAddCommand(TEXT("DebugExecBindings"), TEXT("(Key=F12,Command=\"RenderDoc.CaptureFrame\", Alt=true)"), false);
	}
}

void FRenderDocPluginModule::EndCapture(void* HWnd, const FString& DestPath, ELaunchAfterCapture LaunchOption)
{
	HWND WindowHandle = (HWnd) ? reinterpret_cast<HWND>(HWnd) : GetActiveWindow();

	typedef FRenderDocPluginLoader::RENDERDOC_API_CONTEXT RENDERDOC_API_CONTEXT;
	FRenderDocPluginModule* Plugin = this;
	FRenderDocPluginLoader::RENDERDOC_API_CONTEXT* RenderDocAPILocal = RenderDocAPI;
	ENQUEUE_RENDER_COMMAND(EndRenderDocCapture)(
		[WindowHandle, RenderDocAPILocal, Plugin, DestPath, LaunchOption](FRHICommandListImmediate& RHICmdList)
		{
			FRenderDocFrameCapturer::EndCapture(WindowHandle, RenderDocAPILocal, Plugin, DestPath, LaunchOption);
		});

	DelayedCaptureTick = 0;
	DelayedCaptureSeconds = 0.0;
	CaptureFrameCount = 0;
	CaptureEndTick = 0;
	bPendingCapture = false;
	bCaptureInProgress = false;
}

void FRenderDocPluginModule::CaptureFrame(FViewport* InViewport, const FString& DestPath, ELaunchAfterCapture LaunchOption)
{
	int32 FrameDelay = CVarRenderDocCaptureDelay.GetValueOnAnyThread();

	// Don't do anything if we're currently already waiting for a capture to end : 
	if (bCaptureInProgress)
	{
		return;
	}

	// in case there's no delay and we capture the current viewport, we can trigger the capture immediately : 
	bShouldCaptureAllActivity = ShouldCaptureAllActivity();
	if ((FrameDelay == 0) && !bShouldCaptureAllActivity)
	{
		DoCaptureCurrentViewport(InViewport, DestPath, LaunchOption);
	}
	else
	{
		if (InViewport || !DestPath.IsEmpty() || LaunchOption != ELaunchAfterCapture::Yes)
		{
			UE_LOG(RenderDocPlugin, Warning, TEXT("Deferred captures only support default params. Passed in params will be ignored."));
		}

		// store all CVars at beginning of capture in case they change while the capture is occuring : 
		CaptureFrameCount = CVarRenderDocCaptureFrameCount.GetValueOnAnyThread();
		bCaptureDelayInSeconds = CVarRenderDocCaptureDelayInSeconds.GetValueOnAnyThread() > 0;

		if (bCaptureDelayInSeconds)
		{
			DelayedCaptureSeconds = FPlatformTime::Seconds() + (double)FrameDelay;
		}
		else
		{
			// Begin tracking the global tick counter so that the Tick() method below can
			// identify the beginning and end of a complete engine update cycle.
			// NOTE: GFrameCounter counts engine ticks, while GFrameNumber counts render
			// frames. Multiple frames might get rendered in a single engine update tick.
			// All active windows are updated, in a round-robin fashion, within a single
			// engine tick. This includes thumbnail images for material preview, material
			// editor previews, cascade/persona previes, etc.
			DelayedCaptureTick = GFrameCounter + FrameDelay;
		}

		bPendingCapture = true;
	}
}

void FRenderDocPluginModule::CapturePIE(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: renderdoc.CapturePIE NumFrames"));
		return;
	}

	int32 NumFrames = FCString::Atoi(*Args[0]);
	if (NumFrames < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("NumFrames must be greater than 0."));
		return;
	}

	void* PIEViewportHandle = GetActiveWindow();
	RenderDocAPI->SetActiveWindow(FRenderDocFrameCapturer::GetRenderdocDevicePointer(), PIEViewportHandle);
	RenderDocAPI->TriggerMultiFrameCapture(NumFrames);

	// Wait one frame before starting the PIE session, because RenderDoc will start capturing after the next
	// present, and we want to catch the first PIE frame.
	StartPIEDelayFrames = 1;
}

void FRenderDocPluginModule::DoCaptureCurrentViewport(FViewport* InViewport, const FString& DestPath, ELaunchAfterCapture LaunchOption)
{
	BeginCapture();

	// infer the intended viewport to intercept/capture:
	FViewport* Viewport = InViewport;

	check(GEngine);
	if (!Viewport && GEngine->GameViewport)
	{
		check(GEngine->GameViewport->Viewport);
		if (GEngine->GameViewport->Viewport->HasFocus())
		{
			Viewport = GEngine->GameViewport->Viewport;
		}
	}

#if WITH_EDITOR
	if (!Viewport && GEditor)
	{
		// WARNING: capturing from a "PIE-Eject" Editor viewport will not work as
		// expected; in such case, capture via the console command
		// (this has something to do with the 'active' editor viewport when the UI
		// button is clicked versus the one which the console is attached to)
		Viewport = GEditor->GetActiveViewport();
	}
#endif

	check(Viewport);
	Viewport->Draw(true);

	EndCapture(Viewport->GetWindow(), DestPath, LaunchOption);
}

void FRenderDocPluginModule::Tick(float DeltaTime)
{
#if WITH_EDITOR
	if (StartPIEDelayFrames > 0)
	{
		--StartPIEDelayFrames;
	}
	else if(StartPIEDelayFrames == 0)
	{
		UEditorEngine* EditorEngine = CastChecked<UEditorEngine>(GEngine);
		FRequestPlaySessionParams SessionParams;
		FLevelEditorModule& LevelEditorModule = FModuleManager::Get().GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		SessionParams.DestinationSlateViewport = LevelEditorModule.GetFirstActiveViewport();
		EditorEngine->RequestPlaySession(SessionParams);
		StartPIEDelayFrames = -1;
	}
#endif

	if (!bPendingCapture && !bCaptureInProgress)
		return;

	if (bPendingCapture)
	{
		check(!bCaptureInProgress); // can't be in progress and pending at the same time

		bool bStartCapturing = false;
		if (bCaptureDelayInSeconds)
		{
			bStartCapturing = FPlatformTime::Seconds() > DelayedCaptureSeconds;
		}
		else
		{
			const int64 TickDiff = GFrameCounter - DelayedCaptureTick;
			bStartCapturing = (TickDiff == 1);
		}

		if (bStartCapturing)
		{
			// are we capturing only the current viewport?
			if (!bShouldCaptureAllActivity)
			{
				DoCaptureCurrentViewport(nullptr, FString(), ELaunchAfterCapture::Yes);
				check(!bCaptureInProgress && !bPendingCapture); // EndCapture must have been called
			}
			else
			{
				BeginCapture();
				// from now on, we'll detect the end of the capture by counting ticks : 
				CaptureEndTick = GFrameCounter + CaptureFrameCount + 1;
				bCaptureInProgress = true;
				bPendingCapture = false;
			}
		}
		else
		{
			float TimeLeft = bCaptureDelayInSeconds ? (float)(DelayedCaptureSeconds - FPlatformTime::Seconds()) : (float)(DelayedCaptureTick - GFrameCounter);
			const FText& SecondsOrFrames = bCaptureDelayInSeconds ? LOCTEXT("RenderDocSeconds", "seconds") : LOCTEXT("RenderDocFrames", "frames");

			ShowNotification(LOCGEN_FORMAT_ORDERED(LOCTEXT("RenderDocPendingCaptureNotification", "RenderDoc capture starting in {0} {1}"), TimeLeft, SecondsOrFrames), false);
		}
	}

	const int64 TickDiff = GFrameCounter - DelayedCaptureTick;
	if (bCaptureInProgress)
	{
		check(!bPendingCapture); // can't be in progress and pending at the same time

		if (GFrameCounter == CaptureEndTick)
		{
			EndCapture(nullptr, FString(), ELaunchAfterCapture::Yes);
		}
		else
		{
			ShowNotification(LOCGEN_FORMAT_ORDERED(LOCTEXT("RenderDocCaptureInProgressNotification", "RenderDoc capturing frame #{0}"),
				CaptureFrameCount - (CaptureEndTick - 1 - GFrameCounter)), false);
		}
	}
}

void FRenderDocPluginModule::StartRenderDoc(FString LaunchPath)
{
	ShowNotification(LOCTEXT("RenderDocLaunchRenderDocNotification", "Launching RenderDoc GUI"), true);

	if (LaunchPath.IsEmpty())
	{
		LaunchPath = GetNewestCapture();
	}

	FString ArgumentString = FString::Printf(TEXT("\"%s\""), *LaunchPath);

	if (!LaunchPath.IsEmpty())
	{
		if (!RenderDocAPI->IsRemoteAccessConnected())
		{
			uint32 PID = RenderDocAPI->LaunchReplayUI(true, TCHAR_TO_ANSI(*ArgumentString));

			if (0 == PID)
			{
				UE_LOG(LogTemp, Error, TEXT("Could not launch RenderDoc!!"));
				ShowNotification(LOCTEXT("RenderDocLaunchRenderDocNotificationFailure", "Failed to launch RenderDoc GUI"), true);
			}
		}
	}

	ShowNotification(LOCTEXT("RenderDocLaunchRenderDocNotificationCompleted", "RenderDoc GUI Launched!"), true);
}

FString FRenderDocPluginModule::GetNewestCapture()
{
	char LogFile[512];
	uint64_t Timestamp;
	uint32_t LogPathLength = 512;
	uint32_t Index = 0;
	FString OutString;
	
	while (RenderDocAPI->GetCapture(Index, LogFile, &LogPathLength, &Timestamp))
	{
		OutString = FString(LogPathLength, ANSI_TO_TCHAR(LogFile));

		Index++;
	}
	
	return FPaths::ConvertRelativePathToFull(OutString);
}

void FRenderDocPluginModule::ShutdownModule()
{
	if (GUsingNullRHI)
		return;

	UnBindCaptureCallbacks();

#if WITH_EDITOR
	delete EditorExtensions;
#endif

	Loader.Release();

	RenderDocAPI = nullptr;
}

void FRenderDocPluginModule::BeginCaptureBracket(FRHICommandListImmediate* RHICommandList)
{
	RENDERDOC_DevicePointer Device = FRenderDocFrameCapturer::GetRenderdocDevicePointer();
	RHICommandList->SubmitCommandsAndFlushGPU();
	RHICommandList->EnqueueLambda([this, Device](FRHICommandListImmediate& CmdList)
	{
		RenderDocAPI->StartFrameCapture(Device, NULL);
	});
}

void FRenderDocPluginModule::EndCaptureBracket(FRHICommandListImmediate* RHICommandList)
{
	RENDERDOC_DevicePointer Device = FRenderDocFrameCapturer::GetRenderdocDevicePointer();
	RHICommandList->SubmitCommandsAndFlushGPU();
	RHICommandList->EnqueueLambda([this, Device](FRHICommandListImmediate& CmdList)
	{
		uint32 result = RenderDocAPI->EndFrameCapture(Device, NULL);
		if (result == 1)
		{
			TGraphTask<FRenderDocAsyncGraphTask>::CreateTask().ConstructAndDispatchWhenReady(ENamedThreads::GameThread, [this]()
			{
				StartRenderDoc(FString());
			});
		}
	});
}

void FRenderDocPluginModule::BindCaptureCallbacks()
{
	RenderCaptureInterface::RegisterCallbacks(
		RenderCaptureInterface::FOnBeginCaptureDelegate::CreateLambda([this](FRHICommandListImmediate* RHICommandList, TCHAR const* Name)
		{
			BeginCaptureBracket(RHICommandList);
		}),
		RenderCaptureInterface::FOnEndCaptureDelegate::CreateLambda([this](FRHICommandListImmediate* RHICommandList)
		{
			EndCaptureBracket(RHICommandList);
		})
	);

	if (FAutomationTestFramework::Get().OnCaptureFrameTrace.IsBound())
	{
		UE_LOG(RenderDocPlugin, Warning, TEXT("Automation OnCaptureFrameTrace delegate already bound. RenderDoc capture will not be bound."));
	}
	else
	{
		FAutomationTestFramework::Get().OnCaptureFrameTrace.BindLambda(
			[](const FString& DestPath, FViewport* InViewport)
			{
				FRenderDocPluginModule& PluginModule = FModuleManager::GetModuleChecked<FRenderDocPluginModule>("RenderDocPlugin");
				PluginModule.CaptureFrame(InViewport, DestPath, ELaunchAfterCapture::No);
			});
	}
}

void FRenderDocPluginModule::UnBindCaptureCallbacks()
{
	RenderCaptureInterface::UnregisterCallbacks();
}

#undef LOCTEXT_NAMESPACE

#include "Windows/HideWindowsPlatformTypes.h"

IMPLEMENT_MODULE(FRenderDocPluginModule, RenderDocPlugin)
