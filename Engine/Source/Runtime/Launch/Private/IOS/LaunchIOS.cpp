// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#import <UIKit/UIKit.h>

#include "CoreMinimal.h"
#include "Misc/App.h"
#include "Misc/OutputDeviceError.h"
#include "LaunchEngineLoop.h"
#include "IMessagingModule.h"
#include "IOS/IOSAppDelegate.h"
#include "IOS/IOSView.h"
#include "IOS/IOSCommandLineHelper.h"
#include "GameLaunchDaemonMessageHandler.h"
#include "AudioDevice.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"
#include "IOSAudioDevice.h"
#include "LocalNotification.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "GenericPlatform/GenericApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "MoviePlayer.h"
#include "Containers/Ticker.h"
#include "HAL/ThreadManager.h"
#include "PreLoadScreenManager.h"
#include "TcpConsoleListener.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Misc/EmbeddedCommunication.h"

FEngineLoop GEngineLoop;
FGameLaunchDaemonMessageHandler GCommandSystem;

static int32 DisableAudioSuspendOnAudioInterruptCvar = 1;
FAutoConsoleVariableRef CVarDisableAudioSuspendOnAudioInterrupt(
    TEXT("au.DisableAudioSuspendOnAudioInterrupt"),
    DisableAudioSuspendOnAudioInterruptCvar,
    TEXT("Disables callback for suspending the audio device when we are notified that the audio session has been interrupted.\n")
    TEXT("0: Not Disabled, 1: Disabled"),
    ECVF_Default);

static const double cMaxAudioContextResumeDelay = 0.5;    // Setting this to be 0.5 seconds
static double AudioContextResumeTime = 0;

void FAppEntry::ResetAudioContextResumeTime()
{
	AudioContextResumeTime = 0;
}

void FAppEntry::Suspend(bool bIsInterrupt)
{
	// also treats interrupts BEFORE initializing the engine
	// the movie player gets initialized on the preinit phase, ApplicationHasEnteredForegroundDelegate and ApplicationWillEnterBackgroundDelegate are not yet available
	if (GetMoviePlayer())
	{
		GetMoviePlayer()->Suspend();
	}

	// if background audio is active, then we don't want to do suspend any audio
	if ([[IOSAppDelegate GetDelegate] IsFeatureActive:EAudioFeature::BackgroundAudio] == false)
	{
		if (GEngine && GEngine->GetMainAudioDevice() && !GIsRequestingExit)
		{
			FAudioDevice* AudioDevice = GEngine->GetMainAudioDevice();
			if (bIsInterrupt && DisableAudioSuspendOnAudioInterruptCvar)
			{
				if (FTaskGraphInterface::IsRunning() && !GIsRequestingExit)
				{
					FFunctionGraphTask::CreateAndDispatchWhenReady([AudioDevice]()
					{
						FAudioThread::RunCommandOnAudioThread([AudioDevice]()
						{
							AudioDevice->SetTransientMasterVolume(0.0f);
						}, TStatId());
					}, TStatId(), NULL, ENamedThreads::GameThread);
				}
				else
				{
					AudioDevice->SetTransientMasterVolume(0.0f);
				}
			}
			else
			{
				if (AudioContextResumeTime == 0)
				{
					// wait 0.5 sec before restarting the audio on resume
					// another Suspend event may occur when pulling down the notification center (Suspend-Resume-Suspend)
					AudioContextResumeTime = FPlatformTime::Seconds() + cMaxAudioContextResumeDelay;
				}
				else
				{
					//second resume, restart the audio immediately after resume
					AudioContextResumeTime = 0; 
				}

				if (FTaskGraphInterface::IsRunning())
				{
					FGraphEventRef ResignTask = FFunctionGraphTask::CreateAndDispatchWhenReady([AudioDevice]()
					{
						FAudioThread::RunCommandOnAudioThread([AudioDevice]()
						{
							AudioDevice->SuspendContext();
						}, TStatId());
                
						FAudioCommandFence AudioCommandFence;
						AudioCommandFence.BeginFence();
						AudioCommandFence.Wait();
					}, TStatId(), NULL, ENamedThreads::GameThread);
					
					float BlockTime = [[IOSAppDelegate GetDelegate] GetBackgroundingMainThreadBlockTime];

					// Do not wait forever for this task to complete since the game thread may be stuck on waiting for user input from a modal dialog box
					FEmbeddedCommunication::KeepAwake(TEXT("Background"), false);
					double    startTime = FPlatformTime::Seconds();
					while((FPlatformTime::Seconds() - startTime) < BlockTime)
					{
						FPlatformProcess::Sleep(0.05f);
						if(ResignTask->IsComplete())
						{
							break;
						}
					}
					FEmbeddedCommunication::AllowSleep(TEXT("Background"));
				}
				else
				{
					AudioDevice->SuspendContext();
				}
			}
		}
		else
		{
			int32& SuspendCounter = FIOSAudioDevice::GetSuspendCounter();
			if (SuspendCounter == 0)
			{
				FPlatformAtomics::InterlockedIncrement(&SuspendCounter);
			}
		}
	}
}

void FAppEntry::Resume(bool bIsInterrupt)
{
	if (GetMoviePlayer())
	{
		GetMoviePlayer()->Resume();
	}

	// if background audio is active, then we don't want to do suspend any audio
	// @todo: should this check if we were suspended, in case this changes while in the background? (suspend with background off, but resume with background audio on? is that a thing?)
	if ([[IOSAppDelegate GetDelegate] IsFeatureActive:EAudioFeature::BackgroundAudio] == false)
	{
		if (GEngine && GEngine->GetMainAudioDevice())
		{
			FAudioDevice* AudioDevice = GEngine->GetMainAudioDevice();
        
			if (bIsInterrupt && DisableAudioSuspendOnAudioInterruptCvar)
			{
				if (FTaskGraphInterface::IsRunning())
				{
					FFunctionGraphTask::CreateAndDispatchWhenReady([AudioDevice]()
					{
						FAudioThread::RunCommandOnAudioThread([AudioDevice]()
						{
							AudioDevice->SetTransientMasterVolume(1.0f);
						}, TStatId());
					}, TStatId(), NULL, ENamedThreads::GameThread);
				}
				else
				{
					AudioDevice->SetTransientMasterVolume(1.0f);
				}
			}
			else
			{
				if (AudioContextResumeTime != 0)
				{
					// resume audio on Tick()
					AudioContextResumeTime = FPlatformTime::Seconds() + cMaxAudioContextResumeDelay;
				}
				else
				{
					// resume audio immediately
					ResumeAudioContext();
				}
			}
		}
		else
		{
			int32& SuspendCounter = FIOSAudioDevice::GetSuspendCounter();
			if (SuspendCounter > 0)
			{
				FPlatformAtomics::InterlockedDecrement(&SuspendCounter);
			}
		}
	}
}


void FAppEntry::ResumeAudioContext()
{
	if (GEngine && GEngine->GetMainAudioDevice())
	{
		FAudioDevice* AudioDevice = GEngine->GetMainAudioDevice();
		if (AudioDevice)
		{
			if (FTaskGraphInterface::IsRunning())
			{
				FFunctionGraphTask::CreateAndDispatchWhenReady([AudioDevice]()
				{
					FAudioThread::RunCommandOnAudioThread([AudioDevice]()
					{
						AudioDevice->ResumeContext();
					}, TStatId());
				}, TStatId(), NULL, ENamedThreads::GameThread);
			}
			else
			{
				AudioDevice->ResumeContext();
			}
		}
	}
}

void FAppEntry::RestartAudio()
{
	if (GEngine && GEngine->GetMainAudioDevice())
	{
		FAudioDevice* AudioDevice = GEngine->GetMainAudioDevice();

		if (FTaskGraphInterface::IsRunning())
		{
			int32& SuspendCounter = FIOSAudioDevice::GetSuspendCounter();

			//increment the counter, otherwise ResumeContext won't work
			if (SuspendCounter == 0)
			{
				FPlatformAtomics::InterlockedIncrement(&SuspendCounter);
			}

			FFunctionGraphTask::CreateAndDispatchWhenReady([AudioDevice]()
			{
				FAudioThread::RunCommandOnAudioThread([AudioDevice]()
				{
					AudioDevice->ResumeContext();
				}, TStatId());
			}, TStatId(), NULL, ENamedThreads::GameThread);
		}
		else
		{
			AudioDevice->ResumeContext();
		}
	}
}

void FAppEntry::PreInit(IOSAppDelegate* AppDelegate, UIApplication* Application)
{
	// make a controller object
	IOSViewController* IOSController = [[IOSViewController alloc] init];
	
#if PLATFORM_TVOS
	// @todo tvos: This may need to be exposed to the game so that when you click Menu it will background the app
	// this is basically the same way Android handles the Back button (maybe we should pass Menu button as back... maybe)
	IOSController.controllerUserInteractionEnabled = NO;
#endif
	
	// point to the GL view we want to use
	AppDelegate.RootView = [IOSController view];

	[AppDelegate.Window setRootViewController:IOSController];

	// windows owns it now
//	[IOSController release];

#if !PLATFORM_TVOS
	// reset badge count on launch
	Application.applicationIconBadgeNumber = 0;
#endif
}

static void MainThreadInit()
{
	IOSAppDelegate* AppDelegate = [IOSAppDelegate GetDelegate];

	// Size the view appropriately for any potentially dynamically attached displays,
	// prior to creating any framebuffers
	CGRect MainFrame = [[UIScreen mainScreen] bounds];

	// @todo: use code similar for presizing for secondary screens
// 	CGRect FullResolutionRect =
// 		CGRectMake(
// 		0.0f,
// 		0.0f,
// 		GSystemSettings.bAllowSecondaryDisplays ?
// 		Max<float>(MainFrame.size.width, GSystemSettings.SecondaryDisplayMaximumWidth)	:
// 		MainFrame.size.width,
// 		GSystemSettings.bAllowSecondaryDisplays ?
// 		Max<float>(MainFrame.size.height, GSystemSettings.SecondaryDisplayMaximumHeight) :
// 		MainFrame.size.height
// 		);

	CGRect FullResolutionRect = MainFrame;

	// embedded apps are embedded inside a UE4 view, so it's already made
#if BUILD_EMBEDDED_APP
	// tell the embedded app that the .ini files are ready to be used, ie the View can be made if it was waiting to create the view
	FEmbeddedCallParamsHelper Helper;
	Helper.Command = TEXT("inisareready");
	FEmbeddedDelegates::GetEmbeddedToNativeParamsDelegateForSubsystem(TEXT("native")).Broadcast(Helper);
	// checkf(AppDelegate.IOSView != nil, TEXT("For embedded apps, the UE4EmbeddedView must have been created and set into the AppDelegate as IOSView"));
#else
	AppDelegate.IOSView = [[FIOSView alloc] initWithFrame:FullResolutionRect];
	AppDelegate.IOSView.clearsContextBeforeDrawing = NO;
#if !PLATFORM_TVOS
	AppDelegate.IOSView.multipleTouchEnabled = YES;
#endif

	// add it to the window
	[AppDelegate.RootView addSubview:AppDelegate.IOSView];

	// initialize the backbuffer of the view (so the RHI can use it)
	[AppDelegate.IOSView CreateFramebuffer:YES];
#endif
}


bool FAppEntry::IsStartupMoviePlaying()
{
	return GEngine && GEngine->IsInitialized() && GetMoviePlayer() && GetMoviePlayer()->IsStartupMoviePlaying();
}


void FAppEntry::PlatformInit()
{

	// call a function in the main thread to do some processing that needs to happen there, now that the .ini files are loaded
	dispatch_async(dispatch_get_main_queue(), ^{ MainThreadInit(); });

	// wait until the GLView is fully initialized, so the RHI can be initialized
	IOSAppDelegate* AppDelegate = [IOSAppDelegate GetDelegate];

	while (!AppDelegate.IOSView || !AppDelegate.IOSView->bIsInitialized)
	{
#if BUILD_EMBEDDED_APP
		// while embedded, the native app may be waiting on some processing to happen before showing the view, so we have to let
		// processing occur here
		FTicker::GetCoreTicker().Tick(0.005f);
		FThreadManager::Get().Tick();
#endif
		FPlatformProcess::Sleep(0.005f);
	}

	// set the GL context to this thread
	[AppDelegate.IOSView MakeCurrent];

	// Set GSystemResolution now that we have the size.
	FDisplayMetrics DisplayMetrics;
	FDisplayMetrics::RebuildDisplayMetrics(DisplayMetrics);
	FSystemResolution::RequestResolutionChange(DisplayMetrics.PrimaryDisplayWidth, DisplayMetrics.PrimaryDisplayHeight, EWindowMode::Fullscreen);
	IConsoleManager::Get().CallAllConsoleVariableSinks();
}

extern TcpConsoleListener *ConsoleListener;

void FAppEntry::Init()
{
	SCOPED_BOOT_TIMING("FAppEntry::Init()");

	FPlatformProcess::SetRealTimeMode();

	//extern TCHAR GCmdLine[16384];
	GEngineLoop.PreInit(FCommandLine::Get());

	// initialize messaging subsystem
	FModuleManager::LoadModuleChecked<IMessagingModule>("Messaging");

	//Set up the message handling to interface with other endpoints on our end.
	NSLog(@"%s", "Initializing ULD Communications in game mode\n");
	GCommandSystem.Init();

	GLog->SetCurrentThreadAsMasterThread();
	
	// Send the launch local notification to the local notification service now that the engine module system has been initialized
	if(gAppLaunchedWithLocalNotification)
	{
		ILocalNotificationService* notificationService = NULL;

		// Get the module name from the .ini file
		FString ModuleName;
		GConfig->GetString(TEXT("LocalNotification"), TEXT("DefaultPlatformService"), ModuleName, GEngineIni);

		if (ModuleName.Len() > 0)
		{			
			// load the module by name retrieved from the .ini
			ILocalNotificationModule* module = FModuleManager::LoadModulePtr<ILocalNotificationModule>(*ModuleName);

			// does the module exist?
			if (module != nullptr)
			{
				notificationService = module->GetLocalNotificationService();
				if(notificationService != NULL)
				{
					notificationService->SetLaunchNotification(gLaunchLocalNotificationActivationEvent, gLaunchLocalNotificationFireDate);
				}
			}
		}
	}

	// start up the engine
	GEngineLoop.Init();
#if !UE_BUILD_SHIPPING
	UE_LOG(LogInit, Display, TEXT("Initializing TCPConsoleListener."));
	if (ConsoleListener == nullptr)
	{
		FIPv4Endpoint ConsoleTCP(FIPv4Address::InternalLoopback, 8888); //TODO: @csulea read this from some .ini
		ConsoleListener = new TcpConsoleListener(ConsoleTCP);
	}
#endif // UE_BUILD_SHIPPING
}

static FSuspendRenderingThread* SuspendThread = NULL;

void FAppEntry::Tick()
{
    if (SuspendThread != NULL)
    {
        delete SuspendThread;
        SuspendThread = NULL;
        FPlatformProcess::SetRealTimeMode();
    }
    
	if (AudioContextResumeTime != 0)
	{
		if (FPlatformTime::Seconds() >= AudioContextResumeTime)
		{
			ResumeAudioContext();
			AudioContextResumeTime = 0;
		}
	}

	// tick the engine
	GEngineLoop.Tick();
}

void FAppEntry::SuspendTick()
{
    if (!SuspendThread)
    {
        SuspendThread = new FSuspendRenderingThread(true);
    }
    
    FPlatformProcess::Sleep(0.1f);
}

void FAppEntry::Shutdown()
{
	if (ConsoleListener)
	{
		delete ConsoleListener;
	}
	NSLog(@"%s", "Shutting down Game ULD Communications\n");
	GCommandSystem.Shutdown();
    
    // kill the engine
    GEngineLoop.Exit();
}

bool	FAppEntry::gAppLaunchedWithLocalNotification;
FString	FAppEntry::gLaunchLocalNotificationActivationEvent;
int32	FAppEntry::gLaunchLocalNotificationFireDate;

FString GSavedCommandLine;

#if !BUILD_EMBEDDED_APP

int main(int argc, char *argv[])
{
    for(int Option = 1; Option < argc; Option++)
	{
		GSavedCommandLine += TEXT(" ");
		GSavedCommandLine += UTF8_TO_TCHAR(argv[Option]);
	}

	// convert $'s to " because Xcode swallows the " and this will allow -execcmds= to be usable from xcode
	GSavedCommandLine = GSavedCommandLine.Replace(TEXT("$"), TEXT("\""));

	FIOSCommandLineHelper::InitCommandArgs(FString());
	
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("WaitForDebugger")))
    {
        while(!FPlatformMisc::IsDebuggerPresent())
        {
            FPlatformMisc::LowLevelOutputDebugString(TEXT("Waiting for debugger...\n"));
            FPlatformProcess::Sleep(1.f);
        }
        FPlatformMisc::LowLevelOutputDebugString(TEXT("Debugger attached.\n"));
    }
#endif
    
    
	@autoreleasepool {
	    return UIApplicationMain(argc, argv, nil, NSStringFromClass([IOSAppDelegate class]));
	}
}

#endif
