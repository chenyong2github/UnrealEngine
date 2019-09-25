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
		GConfig->GetBool(TEXT("VoiceChat.Vivox"), TEXT("bEnableBluetoothMicrophone"), bEnableBluetoothMicrophone, GEngineIni);

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

		UpdateVoiceChatSettings();
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

	bool bReturn = FVivoxVoiceChat::Uninitialize();

	while (VoiceChatEnableCount > 0)
	{
		EnableVoiceChat(false);
	}

	return bReturn;
}

void FIOSVivoxVoiceChat::SetSetting(const FString& Name, const FString& Value)
{
	if (Name == TEXT("HardwareAEC"))
	{
		OverrideEnableHardwareAEC = FCString::ToBool(*Value);
		UpdateVoiceChatSettings();
	}
	else if (Name == TEXT("BluetoothMicrophone"))
	{
		OverrideEnableBluetoothMicrophone = FCString::ToBool(*Value);
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
		return LexToString(IsHardwareAECEnabled());
	}
	else if (Name == TEXT("BluetoothMicrophone"))
	{
		return LexToString(IsBluetoothMicrophoneEnabled());
	}
	return FVivoxVoiceChat::GetSetting(Name);
}

void FIOSVivoxVoiceChat::JoinChannel(const FString& ChannelName, const FString& ChannelCredentials, EVoiceChatChannelType ChannelType, const FOnVoiceChatChannelJoinCompleteDelegate& Delegate, TOptional<FVoiceChatChannel3dProperties> Channel3dProperties)
{
	EnableVoiceChat(true);

	FOnVoiceChatChannelJoinCompleteDelegate DelegateWrapper = FOnVoiceChatChannelJoinCompleteDelegate::CreateLambda(
		[this, Delegate](const FString& ChannelName, const FVoiceChatResult& Result)
		{
			if (!Result.bSuccess)
			{
				EnableVoiceChat(false);
			}
			Delegate.ExecuteIfBound(ChannelName, Result);
		});

	FVivoxVoiceChat::JoinChannel(ChannelName, ChannelCredentials, ChannelType, DelegateWrapper, Channel3dProperties);
}

FDelegateHandle FIOSVivoxVoiceChat::StartRecording(const FOnVoiceChatRecordSamplesAvailableDelegate::FDelegate& Delegate)
{
	bIsRecording = true;
	EnableVoiceChat(true);
	return FVivoxVoiceChat::StartRecording(Delegate);
}

void FIOSVivoxVoiceChat::StopRecording(FDelegateHandle Handle)
{
	FVivoxVoiceChat::StopRecording(Handle);
	EnableVoiceChat(false);
	bIsRecording = false;
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
	FVivoxVoiceChat::onChannelJoined(AccountName, ChannelUri);
}

void FIOSVivoxVoiceChat::onChannelExited(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status)
{
	EnableVoiceChat(false);
	FVivoxVoiceChat::onChannelExited(AccountName, ChannelUri, Status);
}

void FIOSVivoxVoiceChat::onDisconnected(const VivoxClientApi::Uri& Server, const VivoxClientApi::VCSStatus& Status)
{
	while (VoiceChatEnableCount > (bIsRecording ? 1 : 0))
	{
		// call once for every channel we were in
		EnableVoiceChat(false);
	}
	FVivoxVoiceChat::onDisconnected(Server, Status);
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

bool FIOSVivoxVoiceChat::IsBluetoothMicrophoneEnabled() const
{
	return OverrideEnableBluetoothMicrophone.Get(bEnableBluetoothMicrophone);
}

void FIOSVivoxVoiceChat::EnableVoiceChat(bool bEnable)
{
	if (bEnable)
	{
		++VoiceChatEnableCount;
		if (VoiceChatEnableCount == 1)
		{
			[[IOSAppDelegate GetDelegate] SetFeature:EAudioFeature::Playback Active:true];
			[[IOSAppDelegate GetDelegate] SetFeature:EAudioFeature::Record Active:true];

			const bool bEnableAEC = IsHardwareAECEnabled() && IsUsingBuiltInSpeaker();
			vx_set_platform_aec_enabled(bEnableAEC ? 1 : 0);
		}
	}
	else
	{
		if (ensureMsgf(VoiceChatEnableCount > 0, TEXT("Attempted to disable voice chat when it was already disabled")))
		{
			--VoiceChatEnableCount;
			if (VoiceChatEnableCount == 0)
			{
				vx_set_platform_aec_enabled(0);
				[[IOSAppDelegate GetDelegate] SetFeature:EAudioFeature::Record Active:false];
				[[IOSAppDelegate GetDelegate] SetFeature:EAudioFeature::Playback Active:false];
			}
		}
	}
}

void FIOSVivoxVoiceChat::UpdateVoiceChatSettings()
{
	if (bBluetoothMicrophoneFeatureEnabled != IsBluetoothMicrophoneEnabled())
	{
		[[IOSAppDelegate GetDelegate] SetFeature:EAudioFeature::BluetoothMicrophone Active:IsBluetoothMicrophoneEnabled()];
		bBluetoothMicrophoneFeatureEnabled = IsBluetoothMicrophoneEnabled();
	}

	const bool bEnableAEC = IsHardwareAECEnabled() && IsUsingBuiltInSpeaker();

	if (bVoiceChatModeEnabled != bEnableAEC)
	{
		[[IOSAppDelegate GetDelegate] SetFeature:EAudioFeature::VoiceChat Active:bEnableAEC];
		bVoiceChatModeEnabled = bEnableAEC;
	}

	if (VoiceChatEnableCount > 0)
	{
		UE_LOG(LogVivoxVoiceChat, Verbose, TEXT("%s AEC"), bEnableAEC ? TEXT("Enabling") : TEXT("Disabling"))
		vx_set_platform_aec_enabled(bEnableAEC ? 1 : 0);
	}
}

bool FIOSVivoxVoiceChat::IsUsingBuiltInSpeaker()
{
	if (AVAudioSessionRouteDescription* CurrentRoute = [[AVAudioSession sharedInstance] currentRoute])
	{
		for (AVAudioSessionPortDescription* Port in [CurrentRoute outputs])
		{
			if ([[Port portType] isEqualToString:AVAudioSessionPortBuiltInReceiver]
				|| [[Port portType] isEqualToString:AVAudioSessionPortBuiltInSpeaker])
			{
				return true;
			}
		}
	}

	return false;
}
