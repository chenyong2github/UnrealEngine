// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VivoxVoiceChat.h"

class FIOSVivoxVoiceChat;

class FIOSVivoxVoiceChatUser : public FVivoxVoiceChatUser
{
public:
	FIOSVivoxVoiceChatUser(FVivoxVoiceChat& InVivoxVoiceChat);
	void Initialize();

	bool IsRecording() const { return bIsRecording; }

	// ~Begin IVoiceChatUser Interface
	virtual void SetSetting(const FString& Name, const FString& Value) override;
	virtual FString GetSetting(const FString& Name) override;
	virtual void JoinChannel(const FString& ChannelName, const FString& ChannelCredentials, EVoiceChatChannelType ChannelType, const FOnVoiceChatChannelJoinCompleteDelegate& Delegate, TOptional<FVoiceChatChannel3dProperties> Channel3dProperties = TOptional<FVoiceChatChannel3dProperties>()) override;
	virtual FDelegateHandle StartRecording(const FOnVoiceChatRecordSamplesAvailableDelegate::FDelegate& Delegate) override;
	virtual void StopRecording(FDelegateHandle Handle) override;
	// ~End IVoiceChat Interface
	
	FIOSVivoxVoiceChat& GetIOSVoiceChat();
	
private:
	virtual void onChannelExited(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status) override;
	
	bool bIsRecording;
};


class FIOSVivoxVoiceChat : public FVivoxVoiceChat
{
public:
	FIOSVivoxVoiceChat();
	virtual ~FIOSVivoxVoiceChat();

	// ~Begin IVoiceChat Interface
	virtual bool Initialize() override;
	virtual bool Uninitialize() override;
	virtual IVoiceChatUser* CreateUser() override;
	// ~End IVoiceChat Interface

	bool IsHardwareAECEnabled() const;
	void SetHardwareAECEnabled(bool bEnabled);
	bool IsBluetoothMicrophoneEnabled() const;
	void SetBluetoothMicrophoneEnabled(bool bEnabled);
	void EnableVoiceChat(bool bEnable);

protected:
	// ~Begin DebugClientApiEventHandler Interface
	virtual void InvokeOnUIThread(void (Func)(void* Arg0), void* Arg0) override;
	virtual void onDisconnected(const VivoxClientApi::Uri& Server, const VivoxClientApi::VCSStatus& Status) override;
	// ~End DebugClientApiEventHandler Interface

	void SetVivoxSdkConfigHints(vx_sdk_config_t &Hints) override;

private:
	void OnVoiceChatConnectComplete(const FVoiceChatResult& Result);
	void OnVoiceChatDisconnectComplete(const FVoiceChatResult& Result);
	void OnVoiceChatDelayedDisconnectComplete(const FVoiceChatResult& Result);

	void HandleApplicationWillEnterBackground();
	void HandleApplicationHasEnteredForeground();
	void HandleAudioRouteChanged(bool);

	void Reconnect();

	FDelegateHandle ApplicationWillEnterBackgroundHandle;
	FDelegateHandle ApplicationDidEnterForegroundHandle;
	FDelegateHandle AudioRouteChangedHandle;

	UIBackgroundTaskIdentifier BGTask;
	bool bDisconnectInBackground;
	bool bInBackground;
	bool bShouldReconnect;

	float BackgroundDelayedDisconnectTime;
	NSTimer* DelayedDisconnectTimer;

	uint VoiceChatEnableCount = 0;

	bool bEnableHardwareAEC = false;
	TOptional<bool> OverrideEnableHardwareAEC;
	bool bVoiceChatModeEnabled = false;

	bool bEnableBluetoothMicrophone = false;
	TOptional<bool> OverrideEnableBluetoothMicrophone;
	bool bBluetoothMicrophoneFeatureEnabled = false;

	void UpdateVoiceChatSettings();
	bool IsUsingBuiltInSpeaker();
};
