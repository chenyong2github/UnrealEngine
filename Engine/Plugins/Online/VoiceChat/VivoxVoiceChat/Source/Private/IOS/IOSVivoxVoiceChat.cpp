// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IOSVivoxVoiceChat.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EmbeddedCommunication.h"
#include "IOS/IOSAppDelegate.h"

TUniquePtr<FVivoxVoiceChat> CreateVivoxObject()
{
	return MakeUnique<FIOSVivoxVoiceChat>();
}

FIOSVivoxVoiceChat::FIOSVivoxVoiceChat()
	: BGTask(UIBackgroundTaskInvalid)
	, bDisconnectInBackground(true)
	, bInBackground(false)
	, bShouldReconnect(false)
	, bIsRecording(false)
	, BackgroundDelayedDisconnectTime(0.0f)
	, DelayedDisconnectTimer(nullptr)
{
}

FIOSVivoxVoiceChat::~FIOSVivoxVoiceChat()
{
}

bool FIOSVivoxVoiceChat::Initialize()
{
	bool bResult = FVivoxVoiceChat::Initialize();

	if (bResult)
	{
		GConfig->GetBool(TEXT("VoiceChat.Vivox"), TEXT("bDisconnectInBackground"), bDisconnectInBackground, GEngineIni);
		GConfig->GetFloat(TEXT("VoiceChat.Vivox"), TEXT("BackgroundDelayedDisconnectTime"), BackgroundDelayedDisconnectTime, GEngineIni);
		GConfig->GetBool(TEXT("VoiceChat.Vivox"), TEXT("bEnableHardwareAEC"), bEnableHardwareAEC, GEngineIni);
		
		if (!ApplicationWillEnterBackgroundHandle.IsValid())
		{
			ApplicationWillEnterBackgroundHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddRaw(this, &FIOSVivoxVoiceChat::HandleApplicationWillEnterBackground);
		}
		if (!ApplicationDidEnterForegroundHandle.IsValid())
		{
			ApplicationDidEnterForegroundHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddRaw(this, &FIOSVivoxVoiceChat::HandleApplicationHasEnteredForeground);
		}
		if (!AudioRouteChangedHandle.IsValid())
		{
			AudioRouteChangedHandle = FCoreDelegates::AudioRouteChangedDelegate.AddRaw(this, &FIOSVivoxVoiceChat::HandleAudioRouteChanged);
		}
	}

	bInBackground = false;
	bShouldReconnect = false;
	bIsRecording = false;

	return bResult;
}

bool FIOSVivoxVoiceChat::Uninitialize()
{
	if (ApplicationWillEnterBackgroundHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(ApplicationWillEnterBackgroundHandle);
		ApplicationWillEnterBackgroundHandle.Reset();
	}
	if (ApplicationDidEnterForegroundHandle.IsValid())
	{
		FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ApplicationDidEnterForegroundHandle);
		ApplicationDidEnterForegroundHandle.Reset();
	}
	if (AudioRouteChangedHandle.IsValid())
	{
		FCoreDelegates::AudioRouteChangedDelegate.Remove(AudioRouteChangedHandle);
		AudioRouteChangedHandle.Reset();
	}

	return FVivoxVoiceChat::Uninitialize();
}

void FIOSVivoxVoiceChat::SetSetting(const FString& Name, const FString& Value)
{
	if (Name == TEXT("HardwareAEC"))
	{
		OverrideEnableHardwareAEC = FCString::ToBool(*Value);
		UpdateVoiceChatSettings();
	}
	else
	{
		FVivoxVoiceChat::SetSetting(Name, Value);
	}
}

FString FIOSVivoxVoiceChat::GetSetting(const FString& Name)
{
	if (Name == TEXT("HardwareAEC"))
	{
		return LexToString(OverrideEnableHardwareAEC.Get(bEnableHardwareAEC));
	}
	return FVivoxVoiceChat::GetSetting(Name);
}

FDelegateHandle FIOSVivoxVoiceChat::StartRecording(const FOnVoiceChatRecordSamplesAvailableDelegate::FDelegate& Delegate)
{
	EnableVoiceChat(true);
	return FVivoxVoiceChat::StartRecording(Delegate);
}

void FIOSVivoxVoiceChat::StopRecording(FDelegateHandle Handle)
{
	FVivoxVoiceChat::StopRecording(Handle);
	if (ConnectionState < EConnectionState::Connecting)
	{
		EnableVoiceChat(false);
	}
}

void FIOSVivoxVoiceChat::InvokeOnUIThread(void (Func)(void* Arg0), void* Arg0)
{
	[FIOSAsyncTask CreateTaskWithBlock:^bool()
	{
		if (Func)
		{
			(*Func)(Arg0);
		}
		return true;
	}];
	
	FEmbeddedCommunication::WakeGameThread();
}

void FIOSVivoxVoiceChat::onChannelJoined(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri)
{
	EnableVoiceChat(true);
	FVivoxVoiceChat::onChannelJoined(AccountName, ChannelUri);
}

void FIOSVivoxVoiceChat::onChannelExited(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status)
{
	FVivoxVoiceChat::onChannelExited(AccountName, ChannelUri, Status);
	
	bool bIsInChannel = false;
	for (const TPair<FString,FChannelSession>& ChannelSession : LoginSession.ChannelSessions)
	{
		if (ChannelSession.Value.State == FChannelSession::EState::Connected)
		{
			bIsInChannel = true;
		}
	}
	if (!bIsRecording && !bIsInChannel)
	{
		EnableVoiceChat(false);
	}
}

void FIOSVivoxVoiceChat::onDisconnected(const VivoxClientApi::Uri& Server, const VivoxClientApi::VCSStatus& Status)
{
	FVivoxVoiceChat::onDisconnected(Server, Status);
	if (!bIsRecording)
	{
		EnableVoiceChat(false);
	}
}

void FIOSVivoxVoiceChat::OnVoiceChatConnectComplete(const FVoiceChatResult& Result)
{
	if (Result.bSuccess)
	{
		OnVoiceChatReconnectedDelegate.Broadcast();
	}
	else
	{
		OnVoiceChatDisconnectedDelegate.Broadcast(Result);
	}
}

void FIOSVivoxVoiceChat::OnVoiceChatDisconnectComplete(const FVoiceChatResult& Result)
{
	if (bInBackground)
	{
		bShouldReconnect = true;
	}
	else if (IsInitialized())
	{
		// disconnect complete delegate fired after entering foreground
		Reconnect();
	}
	
	if (BGTask != UIBackgroundTaskInvalid)
	{
		UIApplication* App = [UIApplication sharedApplication];
		[App endBackgroundTask:BGTask];
		BGTask = UIBackgroundTaskInvalid;
	}
}

void FIOSVivoxVoiceChat::OnVoiceChatDelayedDisconnectComplete(const FVoiceChatResult& Result)
{
	OnVoiceChatDisconnectedDelegate.Broadcast(Result);
}

void FIOSVivoxVoiceChat::SetVivoxSdkConfigHints(vx_sdk_config_t &Hints)
{
	Hints.dynamic_voice_processing_switching = 0;
}

void FIOSVivoxVoiceChat::HandleApplicationWillEnterBackground()
{
	UE_LOG(LogVivoxVoiceChat, Log, TEXT("OnApplicationWillEnterBackgroundDelegate"));

	const bool bIsBackgroundAudioEnabled = [[IOSAppDelegate GetDelegate] IsFeatureActive:EAudioFeature::BackgroundAudio];
	if (IsConnected() && bDisconnectInBackground && !bIsBackgroundAudioEnabled)
	{
		if (BGTask != UIBackgroundTaskInvalid)
		{
			UIApplication* App = [UIApplication sharedApplication];
			[App endBackgroundTask : BGTask];
			BGTask = UIBackgroundTaskInvalid;
		}
		UIApplication* App = [UIApplication sharedApplication];
		BGTask = [App beginBackgroundTaskWithName : @"VivoxDisconnect" expirationHandler : ^{
			UE_LOG(LogVivoxVoiceChat, Warning, TEXT("Disconnect operation never completed"));

			[App endBackgroundTask : BGTask];
			BGTask = UIBackgroundTaskInvalid;
		}];

		Disconnect(FOnVoiceChatDisconnectCompleteDelegate::CreateRaw(this, &FIOSVivoxVoiceChat::OnVoiceChatDisconnectComplete));
	}
	else
	{
		if (BackgroundDelayedDisconnectTime > 0.01)
		{
			dispatch_async(dispatch_get_main_queue(), ^
			{
				DelayedDisconnectTimer = [NSTimer scheduledTimerWithTimeInterval:BackgroundDelayedDisconnectTime repeats:NO block:^(NSTimer * _Nonnull timer) {
					[FIOSAsyncTask CreateTaskWithBlock:^bool(void){
						Disconnect(FOnVoiceChatDisconnectCompleteDelegate::CreateRaw(this, &FIOSVivoxVoiceChat::OnVoiceChatDelayedDisconnectComplete));
						return true;
					}];
					DelayedDisconnectTimer = nullptr;
				}];
			});
			
		}
		bShouldReconnect = false;
	}
	
	VivoxClientConnection.EnteredBackground();
}

void FIOSVivoxVoiceChat::HandleApplicationHasEnteredForeground()
{
	UE_LOG(LogVivoxVoiceChat, Log, TEXT("OnApplicationHasEnteredForegoundDelegate"));

	VivoxClientConnection.WillEnterForeground();

	if (BGTask != UIBackgroundTaskInvalid)
	{
		UIApplication* App = [UIApplication sharedApplication];
		[App endBackgroundTask : BGTask];
		BGTask = UIBackgroundTaskInvalid;
	}

	dispatch_async(dispatch_get_main_queue(), ^
	{
		[DelayedDisconnectTimer invalidate];
		DelayedDisconnectTimer = nullptr;
	});

	if (bShouldReconnect)
	{
		Reconnect();
	}

	// HandleAudioRouteChanged is not getting called when a route change happens in the background. Update voice chat settings here to handle this case
	UpdateVoiceChatSettings();
}

void FIOSVivoxVoiceChat::HandleAudioRouteChanged(bool)
{
	UE_LOG(LogVivoxVoiceChat, Verbose, TEXT("Audio route changed"));
	UpdateVoiceChatSettings();
}

void FIOSVivoxVoiceChat::Reconnect()
{
	Connect(FOnVoiceChatConnectCompleteDelegate::CreateRaw(this, &FIOSVivoxVoiceChat::OnVoiceChatConnectComplete));
	bShouldReconnect = false;
}

bool FIOSVivoxVoiceChat::IsHardwareAECEnabled() const
{
	return OverrideEnableHardwareAEC.Get(bEnableHardwareAEC);
}

void FIOSVivoxVoiceChat::EnableVoiceChat(bool bEnable)
{
	if (FPlatformMisc::IsVoiceChatEnabled() != bEnable)
	{
		if (IsHardwareAECEnabled() && !IsBluetoothA2DPInUse())
		{
			[[IOSAppDelegate GetDelegate] EnableHighQualityVoiceChat:bEnable];
			vx_set_platform_aec_enabled(bEnable ? 1 : 0);
		}
		FPlatformMisc::EnableVoiceChat(bEnable);
	}
}

void FIOSVivoxVoiceChat::UpdateVoiceChatSettings()
{
	if (FPlatformMisc::IsVoiceChatEnabled())
	{
		// update the aec settings
		const bool bEnableAEC = IsHardwareAECEnabled() && !IsBluetoothA2DPInUse();
		UE_LOG(LogVivoxVoiceChat, Verbose, TEXT("%s AEC"), bEnableAEC ? TEXT("Enabling") : TEXT("Disabling"))
		[[IOSAppDelegate GetDelegate] EnableHighQualityVoiceChat:bEnableAEC];
		vx_set_platform_aec_enabled(bEnableAEC ? 1 : 0);
		// reenable voice chat to apply the changes
		FPlatformMisc::EnableVoiceChat(true);
	}
}

bool FIOSVivoxVoiceChat::IsBluetoothA2DPInUse()
{
	if (AVAudioSessionRouteDescription* CurrentRoute = [[AVAudioSession sharedInstance] currentRoute])
	{
		for (AVAudioSessionPortDescription* Port in [CurrentRoute outputs])
		{
			if ([[Port portType] isEqualToString:AVAudioSessionPortBluetoothA2DP])
			{
				return true;
			}
		}
	}

	return false;
}
