// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

THIRD_PARTY_INCLUDES_START
#include "vivoxclientapi/debugclientapieventhandler.h"
#include "vivoxclientapi/iclientapieventhandler.h"
#include "vivoxclientapi/clientconnection.h"
THIRD_PARTY_INCLUDES_END

#include "VoiceChat.h"
#include "Stats/Stats.h"
#include "Misc/CoreMisc.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVivoxVoiceChat, Log, All);

DECLARE_STATS_GROUP(TEXT("Vivox"), STATGROUP_Vivox, STATCAT_Advanced);

class VIVOXVOICECHAT_API FVivoxDelegates
{
public:
	/** Delegate called when the status of the audio device has changed. Triggered on platforms that the vivox sdk provides this event for */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAudioUnitCaptureDeviceStatusChanged, int);
	static FOnAudioUnitCaptureDeviceStatusChanged OnAudioUnitCaptureDeviceStatusChanged;
};

class FVivoxVoiceChat;

class FVivoxVoiceChatUser : public IVoiceChatUser
{
public:
	FVivoxVoiceChatUser(FVivoxVoiceChat& InVivoxVoiceChat);
	~FVivoxVoiceChatUser();

	// ~Begin IVoiceChatUser Interface
	virtual void SetSetting(const FString& Name, const FString& Value) override;
	virtual FString GetSetting(const FString& Name) override;
	virtual void SetAudioInputVolume(float Volume) override;
	virtual void SetAudioOutputVolume(float Volume) override;
	virtual float GetAudioInputVolume() const override;
	virtual float GetAudioOutputVolume() const override;
	virtual void SetAudioInputDeviceMuted(bool bIsMuted) override;
	virtual void SetAudioOutputDeviceMuted(bool bIsMuted) override;
	virtual bool GetAudioInputDeviceMuted() const override;
	virtual bool GetAudioOutputDeviceMuted() const override;
	virtual TArray<FString> GetAvailableInputDevices() const override;
	virtual TArray<FString> GetAvailableOutputDevices() const override;
	virtual FOnVoiceChatAvailableAudioDevicesChangedDelegate& OnVoiceChatAvailableAudioDevicesChanged() override { return OnVoiceChatAvailableAudioDevicesChangedDelegate; }
	virtual void SetInputDevice(const FString& InputDevice) override;
	virtual void SetOutputDevice(const FString& OutputDevice) override;
	virtual FString GetInputDevice() const override;
	virtual FString GetOutputDevice() const override;
	virtual FString GetDefaultInputDevice() const override;
	virtual FString GetDefaultOutputDevice() const override;
	virtual void Connect(const FOnVoiceChatConnectCompleteDelegate& Delegate) override;
	virtual void Disconnect(const FOnVoiceChatDisconnectCompleteDelegate& Delegate) override;
	virtual bool IsConnecting() const override;
	virtual bool IsConnected() const override;
	virtual FOnVoiceChatConnectedDelegate& OnVoiceChatConnected() override;
	virtual FOnVoiceChatDisconnectedDelegate& OnVoiceChatDisconnected() override;
	virtual FOnVoiceChatReconnectedDelegate& OnVoiceChatReconnected() override;
	virtual void Login(FPlatformUserId PlatformId, const FString& PlayerName, const FString& Credentials, const FOnVoiceChatLoginCompleteDelegate& Delegate) override;
	virtual void Logout(const FOnVoiceChatLogoutCompleteDelegate& Delegate) override;
	virtual bool IsLoggingIn() const override;
	virtual bool IsLoggedIn() const override;
	virtual FOnVoiceChatLoggedInDelegate& OnVoiceChatLoggedIn() override { return OnVoiceChatLoggedInDelegate; }
	virtual FOnVoiceChatLoggedOutDelegate& OnVoiceChatLoggedOut() override { return OnVoiceChatLoggedOutDelegate; }
	virtual FString GetLoggedInPlayerName() const override;
	virtual void BlockPlayers(const TArray<FString>& PlayerNames) override;
	virtual void UnblockPlayers(const TArray<FString>& PlayerNames) override;
	virtual void JoinChannel(const FString& ChannelName, const FString& ChannelCredentials, EVoiceChatChannelType ChannelType, const FOnVoiceChatChannelJoinCompleteDelegate& Delegate, TOptional<FVoiceChatChannel3dProperties> Channel3dProperties = TOptional<FVoiceChatChannel3dProperties>()) override;
	virtual void LeaveChannel(const FString& Channel, const FOnVoiceChatChannelLeaveCompleteDelegate& Delegate) override;
	virtual FOnVoiceChatChannelJoinedDelegate& OnVoiceChatChannelJoined() override { return OnVoiceChatChannelJoinedDelegate; }
	virtual FOnVoiceChatChannelExitedDelegate& OnVoiceChatChannelExited() override { return OnVoiceChatChannelExitedDelegate; }
	virtual FOnVoiceChatCallStatsUpdatedDelegate& OnVoiceChatCallStatsUpdated() override { return OnVoiceChatCallStatsUpdatedDelegate; }
	virtual void Set3DPosition(const FString& ChannelName, const FVector& SpeakerPosition, const FVector& ListenerPosition, const FVector& ListenerForwardDirection, const FVector& ListenerUpDirection) override;
	virtual TArray<FString> GetChannels() const override;
	virtual TArray<FString> GetPlayersInChannel(const FString& ChannelName) const override;
	virtual EVoiceChatChannelType GetChannelType(const FString& ChannelName) const override;
	virtual FOnVoiceChatPlayerAddedDelegate& OnVoiceChatPlayerAdded() override { return OnVoiceChatPlayerAddedDelegate; }
	virtual FOnVoiceChatPlayerRemovedDelegate& OnVoiceChatPlayerRemoved() override { return OnVoiceChatPlayerRemovedDelegate; }
	virtual bool IsPlayerTalking(const FString& PlayerName) const override;
	virtual FOnVoiceChatPlayerTalkingUpdatedDelegate& OnVoiceChatPlayerTalkingUpdated() override { return OnVoiceChatPlayerTalkingUpdatedDelegate; }
	virtual void SetPlayerMuted(const FString& PlayerName, bool bMuted) override;
	virtual bool IsPlayerMuted(const FString& PlayerName) const override;
	virtual FOnVoiceChatPlayerMuteUpdatedDelegate& OnVoiceChatPlayerMuteUpdated() override { return OnVoiceChatPlayerMuteUpdatedDelegate; }
	virtual void SetPlayerVolume(const FString& PlayerName, float Volume) override;
	virtual float GetPlayerVolume(const FString& PlayerName) const override;
	virtual FOnVoiceChatPlayerVolumeUpdatedDelegate& OnVoiceChatPlayerVolumeUpdated() override { return OnVoiceChatPlayerVolumeUpdatedDelegate; }
	virtual void TransmitToAllChannels() override;
	virtual void TransmitToNoChannels() override;
	virtual void TransmitToSpecificChannel(const FString& ChannelName) override;
	virtual EVoiceChatTransmitMode GetTransmitMode() const override;
	virtual FString GetTransmitChannel() const override;
	virtual FDelegateHandle StartRecording(const FOnVoiceChatRecordSamplesAvailableDelegate::FDelegate& Delegate) override;
	virtual void StopRecording(FDelegateHandle Handle) override;
	virtual FDelegateHandle RegisterOnVoiceChatAfterCaptureAudioReadDelegate(const FOnVoiceChatAfterCaptureAudioReadDelegate::FDelegate& Delegate) override;
	virtual void UnregisterOnVoiceChatAfterCaptureAudioReadDelegate(FDelegateHandle Handle) override;
	virtual FDelegateHandle RegisterOnVoiceChatBeforeCaptureAudioSentDelegate(const FOnVoiceChatBeforeCaptureAudioSentDelegate::FDelegate& Delegate) override;
	virtual void UnregisterOnVoiceChatBeforeCaptureAudioSentDelegate(FDelegateHandle Handle) override;
	virtual FDelegateHandle RegisterOnVoiceChatBeforeRecvAudioRenderedDelegate(const FOnVoiceChatBeforeRecvAudioRenderedDelegate::FDelegate& Delegate) override;
	virtual void UnregisterOnVoiceChatBeforeRecvAudioRenderedDelegate(FDelegateHandle Handle) override;
	virtual FString InsecureGetLoginToken(const FString& PlayerName) override;
	virtual FString InsecureGetJoinToken(const FString& ChannelName, EVoiceChatChannelType ChannelType, TOptional<FVoiceChatChannel3dProperties> Channel3dProperties = TOptional<FVoiceChatChannel3dProperties>()) override;
	// ~End IVoiceChat Interface

protected:
	bool IsInitialized();
	bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar);
	
	friend class FVivoxVoiceChat;
	// ~Begin DebugClientApiEventHandler Interface
	void onConnectCompleted(const VivoxClientApi::Uri& Server);
	void onConnectFailed(const VivoxClientApi::Uri& Server, const VivoxClientApi::VCSStatus& Status);
	void onDisconnected(const VivoxClientApi::Uri& Server, const VivoxClientApi::VCSStatus& Status);
	void onLoginCompleted(const VivoxClientApi::AccountName& AccountName);
	void onInvalidLoginCredentials(const VivoxClientApi::AccountName& AccountName);
	void onLoginFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status);
	void onLogoutCompleted(const VivoxClientApi::AccountName& AccountName);
	void onLogoutFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status);
	void onSessionGroupCreated(const VivoxClientApi::AccountName& AccountName, const char* SessionGroupHandle);
	void onChannelJoined(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri);
	void onInvalidChannelCredentials(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri);
	void onChannelJoinFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status);
	void onChannelExited(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& ReasonCode);
	void onCallStatsUpdated(const VivoxClientApi::AccountName& AccountName, vx_call_stats_t& Stats, bool bIsFinal);
	void onParticipantAdded(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::Uri& ParticipantUri, bool bIsLoggedInUser);
	void onParticipantLeft(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::Uri& ParticipantUri, bool bIsLoggedInUser, VivoxClientApi::IClientApiEventHandler::ParticipantLeftReason Reason);
	void onParticipantUpdated(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::Uri& ParticipantUri, bool bIsLoggedInUser, bool bSpeaking, double MeterEnergy, bool bMutedForAll);
	virtual void onAvailableAudioDevicesChanged();
	void onOperatingSystemChosenAudioInputDeviceChanged(const VivoxClientApi::AudioDeviceId& DeviceId);
	void onSetApplicationChosenAudioInputDeviceCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId);
	void onSetApplicationChosenAudioInputDeviceFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId, const VivoxClientApi::VCSStatus& Status);
	void onOperatingSystemChosenAudioOutputDeviceChanged(const VivoxClientApi::AudioDeviceId& DeviceId);
	void onSetApplicationChosenAudioOutputDeviceCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId);
	void onSetApplicationChosenAudioOutputDeviceFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId, const VivoxClientApi::VCSStatus& Status);
	void onSetParticipantAudioOutputDeviceVolumeForMeCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& TargetUser, const VivoxClientApi::Uri& ChannelUri, int Volume);
	void onSetParticipantAudioOutputDeviceVolumeForMeFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& TargetUser, const VivoxClientApi::Uri& ChannelUri, int Volume, const VivoxClientApi::VCSStatus& Status);
	void onSetChannelAudioOutputDeviceVolumeCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, int Volume);
	void onSetChannelAudioOutputDeviceVolumeFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, int Volume, const VivoxClientApi::VCSStatus& Status);
	void onSetParticipantMutedForMeCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& Target, const VivoxClientApi::Uri& ChannelUri, bool bMuted);
	void onSetParticipantMutedForMeFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& Target, const VivoxClientApi::Uri& ChannelUri, bool bMuted, const VivoxClientApi::VCSStatus& Status);
	void onSetChannelTransmissionToSpecificChannelCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri);
	void onSetChannelTransmissionToSpecificChannelFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status);
	void onSetChannelTransmissionToAllCompleted(const VivoxClientApi::AccountName& AccountName);
	void onSetChannelTransmissionToAllFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status);
	void onSetChannelTransmissionToNoneCompleted(const VivoxClientApi::AccountName& AccountName);
	void onSetChannelTransmissionToNoneFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status);
	void onAudioUnitStarted(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri);
	void onAudioUnitStopped(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri);
	void onAudioUnitAfterCaptureAudioRead(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri, short* PcmFrames, int PcmFrameCount, int AudioFrameRate, int ChannelsPerFrame);
	void onAudioUnitBeforeCaptureAudioSent(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri, short* PcmFrames, int PcmFrameCount, int AudioFrameRate, int ChannelsPerFrame, bool bSpeaking);
	void onAudioUnitBeforeRecvAudioRendered(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri, short* PcmFrames, int PcmFrameCount, int AudioFrameRate, int ChannelsPerFrame, bool bSilence);
	// ~End DebugClientApiEventHandler Interface

	struct FParticipant
	{
		FString PlayerName;
		VivoxClientApi::Uri UserUri;
		bool bTalking = false;
		bool bMuted = false;
		float Volume = 0.5f;
		int IntVolume = 50;
	};

	struct FChannelSession
	{
		FString ChannelName;
		EVoiceChatChannelType ChannelType = EVoiceChatChannelType::NonPositional;
		VivoxClientApi::Uri ChannelUri;
		enum class EState
		{
			Disconnected,
			Disconnecting,
			Connecting,
			Connected
		} State = EState::Disconnected;
		TMap<FString, FParticipant> Participants; // Contains participants in this channel and the current muted/volume/state

		FOnVoiceChatChannelJoinCompleteDelegate JoinDelegate;
		FOnVoiceChatChannelLeaveCompleteDelegate LeaveDelegate;
	};

	struct FLoginSession
	{
		FPlatformUserId PlatformId;
		FString PlayerName;
		VivoxClientApi::AccountName AccountName;
		VivoxClientApi::Uri UserUri;
		enum class EState
		{
			LoggedOut,
			LoggingOut,
			LoggingIn,
			LoggedIn
		} State = EState::LoggedOut;
		TMap<FString, FChannelSession> ChannelSessions;
		TMap<FString, FParticipant> Participants; // Contains participants from all channels and the desired muted/volume state
	};

	struct AudioOptions
	{
		bool bMuted = false;
		float Volume = 1.0f;
	};
	AudioOptions AudioInputOptions;
	AudioOptions AudioOutputOptions;

	VivoxClientApi::ClientConnection& VivoxClientConnection;

	FLoginSession LoginSession;

	// Delegates
	FOnVoiceChatAvailableAudioDevicesChangedDelegate OnVoiceChatAvailableAudioDevicesChangedDelegate;
	FOnVoiceChatLoggedInDelegate OnVoiceChatLoggedInDelegate;
	FOnVoiceChatLoggedOutDelegate OnVoiceChatLoggedOutDelegate;
	FOnVoiceChatChannelJoinedDelegate OnVoiceChatChannelJoinedDelegate;
	FOnVoiceChatChannelExitedDelegate OnVoiceChatChannelExitedDelegate;
	FOnVoiceChatPlayerAddedDelegate OnVoiceChatPlayerAddedDelegate;
	FOnVoiceChatPlayerTalkingUpdatedDelegate OnVoiceChatPlayerTalkingUpdatedDelegate;
	FOnVoiceChatPlayerMuteUpdatedDelegate OnVoiceChatPlayerMuteUpdatedDelegate;
	FOnVoiceChatPlayerVolumeUpdatedDelegate OnVoiceChatPlayerVolumeUpdatedDelegate;
	FOnVoiceChatPlayerRemovedDelegate OnVoiceChatPlayerRemovedDelegate;
	FOnVoiceChatCallStatsUpdatedDelegate OnVoiceChatCallStatsUpdatedDelegate;

	// Recording Delegates and Critical sections
	FCriticalSection AudioRecordLock;
	FOnVoiceChatRecordSamplesAvailableDelegate OnVoiceChatRecordSamplesAvailableDelegate;
	FCriticalSection AfterCaptureAudioReadLock;
	FOnVoiceChatAfterCaptureAudioReadDelegate OnVoiceChatAfterCaptureAudioReadDelegate;
	FCriticalSection BeforeCaptureAudioSentLock;
	FOnVoiceChatBeforeCaptureAudioSentDelegate OnVoiceChatBeforeCaptureAudioSentDelegate;
	FCriticalSection BeforeRecvAudioRenderedLock;
	FOnVoiceChatBeforeRecvAudioRenderedDelegate OnVoiceChatBeforeRecvAudioRenderedDelegate;

	// Completion delegates
	FOnVoiceChatLoginCompleteDelegate OnVoiceChatLoginCompleteDelegate;
	FOnVoiceChatLogoutCompleteDelegate OnVoiceChatLogoutCompleteDelegate;

	FParticipant& GetParticipant(const FString& PlayerName);
	const FParticipant& GetParticipant(const FString& PlayerName) const;

	FChannelSession& GetChannelSession(const FString& ChannelName);
	const FChannelSession& GetChannelSession(const FString& ChannelName) const;
	FChannelSession& GetChannelSession(const VivoxClientApi::Uri& ChannelUri);
	void RemoveChannelSession(const FString& ChannelName);
	void ClearChannelSessions();
	void ClearLoginSession();
	void ApplyAudioInputOptions();
	void ApplyAudioOutputOptions();

	static FString ToString(FLoginSession::EState State);
	static FString ToString(FChannelSession::EState State);

	FVivoxVoiceChat& VivoxVoiceChat;
	FString SessionGroup;
};

class FVivoxVoiceChat : public FSelfRegisteringExec, public IVoiceChat, protected VivoxClientApi::DebugClientApiEventHandler
{
public:
	FVivoxVoiceChat();
	virtual ~FVivoxVoiceChat();

	// IVoiceChat Interface
	virtual bool Initialize() override;
	virtual bool Uninitialize() override;
	virtual bool IsInitialized() const override;
	virtual IVoiceChatUser* CreateUser() override;

	// IVoiceChatUser
	virtual void SetSetting(const FString& Name, const FString& Value) override;
	virtual FString GetSetting(const FString& Name) override;
	virtual void SetAudioInputVolume(float Volume) override;
	virtual void SetAudioOutputVolume(float Volume) override;
	virtual float GetAudioInputVolume() const override;
	virtual float GetAudioOutputVolume() const override;
	virtual void SetAudioInputDeviceMuted(bool bIsMuted) override;
	virtual void SetAudioOutputDeviceMuted(bool bIsMuted) override;
	virtual bool GetAudioInputDeviceMuted() const override;
	virtual bool GetAudioOutputDeviceMuted() const override;
	virtual TArray<FString> GetAvailableInputDevices() const override;
	virtual TArray<FString> GetAvailableOutputDevices() const override;
	virtual FOnVoiceChatAvailableAudioDevicesChangedDelegate& OnVoiceChatAvailableAudioDevicesChanged() override;
	virtual void SetInputDevice(const FString& InputDevice) override;
	virtual void SetOutputDevice(const FString& OutputDevice) override;
	virtual FString GetInputDevice() const override;
	virtual FString GetOutputDevice() const override;
	virtual FString GetDefaultInputDevice() const override;
	virtual FString GetDefaultOutputDevice() const override;
	virtual void Connect(const FOnVoiceChatConnectCompleteDelegate& Delegate) override;
	virtual void Disconnect(const FOnVoiceChatDisconnectCompleteDelegate& Delegate) override;
	virtual bool IsConnecting() const override;
	virtual bool IsConnected() const override;
	virtual FOnVoiceChatConnectedDelegate& OnVoiceChatConnected() override { return OnVoiceChatConnectedDelegate; }
	virtual FOnVoiceChatDisconnectedDelegate& OnVoiceChatDisconnected() override { return OnVoiceChatDisconnectedDelegate; }
	virtual FOnVoiceChatReconnectedDelegate& OnVoiceChatReconnected() override { return OnVoiceChatReconnectedDelegate; }
	virtual void Login(FPlatformUserId PlatformId, const FString& PlayerName, const FString& Credentials, const FOnVoiceChatLoginCompleteDelegate& Delegate) override;
	virtual void Logout(const FOnVoiceChatLogoutCompleteDelegate& Delegate) override;
	virtual bool IsLoggingIn() const override;
	virtual bool IsLoggedIn() const override;
	virtual FOnVoiceChatLoggedInDelegate& OnVoiceChatLoggedIn() override;
	virtual FOnVoiceChatLoggedOutDelegate& OnVoiceChatLoggedOut() override;
	virtual FString GetLoggedInPlayerName() const override;
	virtual void BlockPlayers(const TArray<FString>& PlayerNames) override;
	virtual void UnblockPlayers(const TArray<FString>& PlayerNames) override;
	virtual void JoinChannel(const FString& ChannelName, const FString& ChannelCredentials, EVoiceChatChannelType ChannelType, const FOnVoiceChatChannelJoinCompleteDelegate& Delegate, TOptional<FVoiceChatChannel3dProperties> Channel3dProperties = TOptional<FVoiceChatChannel3dProperties>()) override;
	virtual void LeaveChannel(const FString& Channel, const FOnVoiceChatChannelLeaveCompleteDelegate& Delegate) override;
	virtual FOnVoiceChatChannelJoinedDelegate& OnVoiceChatChannelJoined() override;
	virtual FOnVoiceChatChannelExitedDelegate& OnVoiceChatChannelExited() override;
	virtual FOnVoiceChatCallStatsUpdatedDelegate& OnVoiceChatCallStatsUpdated() override;
	virtual void Set3DPosition(const FString& ChannelName, const FVector& SpeakerPosition, const FVector& ListenerPosition, const FVector& ListenerForwardDirection, const FVector& ListenerUpDirection) override;
	virtual TArray<FString> GetChannels() const override;
	virtual TArray<FString> GetPlayersInChannel(const FString& ChannelName) const override;
	virtual EVoiceChatChannelType GetChannelType(const FString& ChannelName) const override;
	virtual FOnVoiceChatPlayerAddedDelegate& OnVoiceChatPlayerAdded() override;
	virtual FOnVoiceChatPlayerRemovedDelegate& OnVoiceChatPlayerRemoved() override;
	virtual bool IsPlayerTalking(const FString& PlayerName) const override;
	virtual FOnVoiceChatPlayerTalkingUpdatedDelegate& OnVoiceChatPlayerTalkingUpdated() override;
	virtual void SetPlayerMuted(const FString& PlayerName, bool bMuted) override;
	virtual bool IsPlayerMuted(const FString& PlayerName) const override;
	virtual FOnVoiceChatPlayerMuteUpdatedDelegate& OnVoiceChatPlayerMuteUpdated() override;
	virtual void SetPlayerVolume(const FString& PlayerName, float Volume) override;
	virtual float GetPlayerVolume(const FString& PlayerName) const override;
	virtual FOnVoiceChatPlayerVolumeUpdatedDelegate& OnVoiceChatPlayerVolumeUpdated() override;
	virtual void TransmitToAllChannels() override;
	virtual void TransmitToNoChannels() override;
	virtual void TransmitToSpecificChannel(const FString& ChannelName) override;
	virtual EVoiceChatTransmitMode GetTransmitMode() const override;
	virtual FString GetTransmitChannel() const override;
	virtual FDelegateHandle StartRecording(const FOnVoiceChatRecordSamplesAvailableDelegate::FDelegate& Delegate) override;
	virtual void StopRecording(FDelegateHandle Handle) override;
	virtual FDelegateHandle RegisterOnVoiceChatAfterCaptureAudioReadDelegate(const FOnVoiceChatAfterCaptureAudioReadDelegate::FDelegate& Delegate) override;
	virtual void UnregisterOnVoiceChatAfterCaptureAudioReadDelegate(FDelegateHandle Handle) override;
	virtual FDelegateHandle RegisterOnVoiceChatBeforeCaptureAudioSentDelegate(const FOnVoiceChatBeforeCaptureAudioSentDelegate::FDelegate& Delegate) override;
	virtual void UnregisterOnVoiceChatBeforeCaptureAudioSentDelegate(FDelegateHandle Handle) override;
	virtual FDelegateHandle RegisterOnVoiceChatBeforeRecvAudioRenderedDelegate(const FOnVoiceChatBeforeRecvAudioRenderedDelegate::FDelegate& Delegate) override;
	virtual void UnregisterOnVoiceChatBeforeRecvAudioRenderedDelegate(FDelegateHandle Handle) override;
	virtual FString InsecureGetLoginToken(const FString& PlayerName) override;
	virtual FString InsecureGetJoinToken(const FString& ChannelName, EVoiceChatChannelType ChannelType, TOptional<FVoiceChatChannel3dProperties> Channel3dProperties = TOptional<FVoiceChatChannel3dProperties>()) override;
	// ~End IVoiceChat Interface

protected:
	template <typename TFn, typename... TArgs>
	void DispatchUsingAccountName(const TFn& Fn, const VivoxClientApi::AccountName& AccountName, TArgs&&... Args);

	template <typename TFn, typename... TArgs>
	void DispatchAll(const TFn& Fn, TArgs&&... Args);

	// ~Begin DebugClientApiEventHandler Interface
	virtual void InvokeOnUIThread(void (Func)(void* Arg0), void* Arg0) override;
	virtual void onLogStatementEmitted(LogLevel Level, long long NativeMillisecondsSinceEpoch, long ThreadId, const char* LogMessage) override;
	virtual void onConnectCompleted(const VivoxClientApi::Uri& Server) override;
	virtual void onConnectFailed(const VivoxClientApi::Uri& Server, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onDisconnected(const VivoxClientApi::Uri& Server, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onLoginCompleted(const VivoxClientApi::AccountName& AccountName) override;
	virtual void onInvalidLoginCredentials(const VivoxClientApi::AccountName& AccountName) override;
	virtual void onLoginFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onSessionGroupCreated(const VivoxClientApi::AccountName& AccountName, const char* SessionGroupHandle) override;
	virtual void onLogoutCompleted(const VivoxClientApi::AccountName& AccountName) override;
	virtual void onLogoutFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onChannelJoined(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri) override;
	virtual void onInvalidChannelCredentials(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri) override;
	virtual void onChannelJoinFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onChannelExited(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& ReasonCode) override;
	virtual void onCallStatsUpdated(const VivoxClientApi::AccountName& AccountName, vx_call_stats_t& Stats, bool bIsFinal) override;
	virtual void onParticipantAdded(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::Uri& ParticipantUri, bool bIsLoggedInUser) override;
	virtual void onParticipantLeft(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::Uri& ParticipantUri, bool bIsLoggedInUser, ParticipantLeftReason Reason) override;
	virtual void onParticipantUpdated(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::Uri& ParticipantUri, bool bIsLoggedInUser, bool bSpeaking, double MeterEnergy, bool bMutedForAll) override;
	virtual void onAvailableAudioDevicesChanged() override;
	virtual void onOperatingSystemChosenAudioInputDeviceChanged(const VivoxClientApi::AudioDeviceId& DeviceId) override;
	virtual void onSetApplicationChosenAudioInputDeviceCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId) override;
	virtual void onSetApplicationChosenAudioInputDeviceFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onOperatingSystemChosenAudioOutputDeviceChanged(const VivoxClientApi::AudioDeviceId& DeviceId) override;
	virtual void onSetApplicationChosenAudioOutputDeviceCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId) override;
	virtual void onSetApplicationChosenAudioOutputDeviceFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onSetParticipantAudioOutputDeviceVolumeForMeCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& TargetUser, const VivoxClientApi::Uri& ChannelUri, int Volume) override;
	virtual void onSetParticipantAudioOutputDeviceVolumeForMeFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& TargetUser, const VivoxClientApi::Uri& ChannelUri, int Volume, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onSetChannelAudioOutputDeviceVolumeCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, int Volume) override;
	virtual void onSetChannelAudioOutputDeviceVolumeFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, int Volume, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onSetParticipantMutedForMeCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& Target, const VivoxClientApi::Uri& ChannelUri, bool bMuted) override;
	virtual void onSetParticipantMutedForMeFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& Target, const VivoxClientApi::Uri& ChannelUri, bool bMuted, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onSetChannelTransmissionToSpecificChannelCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri) override;
	virtual void onSetChannelTransmissionToSpecificChannelFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onSetChannelTransmissionToAllCompleted(const VivoxClientApi::AccountName& AccountName) override;
	virtual void onSetChannelTransmissionToAllFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onSetChannelTransmissionToNoneCompleted(const VivoxClientApi::AccountName& AccountName) override;
	virtual void onSetChannelTransmissionToNoneFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status) override;
	virtual void onAudioUnitStarted(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri) override;
	virtual void onAudioUnitStopped(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri) override;
	virtual void onAudioUnitAfterCaptureAudioRead(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri, short* PcmFrames, int PcmFrameCount, int AudioFrameRate, int ChannelsPerFrame) override;
	virtual void onAudioUnitBeforeCaptureAudioSent(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri, short* PcmFrames, int PcmFrameCount, int AudioFrameRate, int ChannelsPerFrame, bool bSpeaking) override;
	virtual void onAudioUnitBeforeRecvAudioRendered(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri, short* PcmFrames, int PcmFrameCount, int AudioFrameRate, int ChannelsPerFrame, bool bSilence) override;
	// ~End DebugClientApiEventHandler Interface

	virtual void SetVivoxSdkConfigHints(vx_sdk_config_t& Hints);

	enum class EConnectionState
	{
		Disconnected,
		Disconnecting,
		Connecting,
		Connected
	};

	VivoxClientApi::ClientConnection VivoxClientConnection;

	bool bInitialized;
	EConnectionState ConnectionState;


	// Settings
	FString VivoxServerUrl;
	FString VivoxDomain;
	FString VivoxIssuer;
	FString VivoxNamespace;
	FString VivoxInsecureSecret;
	EVoiceChatAttenuationModel AttenuationModel;
	int MinDistance;
	int MaxDistance;
	float Rolloff;

	VivoxClientApi::AccountName CreateAccountName(const FString& PlayerName);
	FString GetPlayerNameFromAccountName(const VivoxClientApi::AccountName& AccountName);

	VivoxClientApi::Uri CreateUserUri(const FString& PlayerName);
	FString GetPlayerNameFromUri(const VivoxClientApi::Uri& UserUri);

	VivoxClientApi::Uri CreateChannelUri(const FString& ChannelName, EVoiceChatChannelType ChannelType, TOptional<FVoiceChatChannel3dProperties> Channel3dProperties = TOptional<FVoiceChatChannel3dProperties>());
	FString GetChannelNameFromUri(const VivoxClientApi::Uri& ChannelUri);
	EVoiceChatChannelType GetChannelTypeFromUri(const VivoxClientApi::Uri& ChannelUri);

	// Log spam avoidance
	FString LastLogMessage;
	LogLevel LastLogLevel;
	int LogSpamCount = 0;

	// Delegates
	FOnVoiceChatConnectedDelegate OnVoiceChatConnectedDelegate;
	FOnVoiceChatDisconnectedDelegate OnVoiceChatDisconnectedDelegate;
	FOnVoiceChatReconnectedDelegate OnVoiceChatReconnectedDelegate;

	// Completion delegates
	TArray<FOnVoiceChatConnectCompleteDelegate> OnVoiceChatConnectCompleteDelegates;
	TArray<FOnVoiceChatDisconnectCompleteDelegate> OnVoiceChatDisconnectCompleteDelegates;

	// FSelfRegisteringExec Interface
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;

	static FString ToString(EConnectionState State);

	friend FVivoxVoiceChatUser;

	FVivoxVoiceChatUser& GetVoiceChatUser();
	FVivoxVoiceChatUser& GetVoiceChatUser() const;
	void RegisterVoiceChatUser(FVivoxVoiceChatUser* User);
	void UnregisterVoiceChatUser(FVivoxVoiceChatUser* User);
	FCriticalSection VoiceChatUsersCriticalSection;
	TArray<FVivoxVoiceChatUser*> VoiceChatUsers;
	TUniquePtr<FVivoxVoiceChatUser> SingleUserVoiceChatUser;
};
