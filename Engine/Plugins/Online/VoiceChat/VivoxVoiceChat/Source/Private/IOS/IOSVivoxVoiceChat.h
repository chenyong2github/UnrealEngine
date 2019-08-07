// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VivoxVoiceChat.h"

class FIOSVivoxVoiceChat : public FVivoxVoiceChat
{
public:
	FIOSVivoxVoiceChat();
	virtual ~FIOSVivoxVoiceChat();

	// ~Begin IVoiceChat Interface
	virtual bool Initialize() override;
	virtual bool Uninitialize() override;
	virtual void SetSetting(const FString& Name, const FString& Value) override;
	virtual FString GetSetting(const FString& Name) override;
	virtual FDelegateHandle StartRecording(const FOnVoiceChatRecordSamplesAvailableDelegate::FDelegate& Delegate) override;
	virtual void StopRecording(FDelegateHandle Handle) override;
	// ~End IVoiceChat Interface

protected:
	// ~Begin DebugClientApiEventHandler Interface
	virtual void InvokeOnUIThread(void (Func)(void* Arg0), void* Arg0) override;
	virtual void onChannelJoined(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri) override;
	virtual void onChannelExited(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status) override;
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

	bool bIsRecording;

	float BackgroundDelayedDisconnectTime;
	NSTimer* DelayedDisconnectTimer;

	bool IsHardwareAECEnabled() const;
	bool bEnableHardwareAEC = false;
	TOptional<bool> OverrideEnableHardwareAEC;

	void EnableVoiceChat(bool bEnable);
	void UpdateVoiceChatSettings();
	bool IsBluetoothA2DPInUse();
};
