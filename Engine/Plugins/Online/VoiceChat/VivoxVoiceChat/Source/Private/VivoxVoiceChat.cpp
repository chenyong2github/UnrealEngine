// Copyright Epic Games, Inc. All Rights Reserved.

#include "VivoxVoiceChat.h" 

#include "Async/Async.h"
#include "HAL/LowLevelMemTracker.h"
#include "Logging/LogMacros.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EmbeddedCommunication.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "VoiceChatErrors.h"
#include "VivoxVoiceChatErrors.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include "Vxc.h"
#include "VxcErrors.h"

DEFINE_LOG_CATEGORY(LogVivoxVoiceChat);

FVivoxDelegates::FOnAudioUnitCaptureDeviceStatusChanged FVivoxDelegates::OnAudioUnitCaptureDeviceStatusChanged;

namespace
{

FString LexToString(VivoxClientApi::IClientApiEventHandler::ParticipantLeftReason Reason)
{
	switch (Reason)
	{
	case VivoxClientApi::IClientApiEventHandler::ReasonLeft:	return TEXT("Left");
	case VivoxClientApi::IClientApiEventHandler::ReasonNetwork:	return TEXT("Network");
	case VivoxClientApi::IClientApiEventHandler::ReasonKicked:	return TEXT("Kicked");
	case VivoxClientApi::IClientApiEventHandler::ReasonBanned:	return TEXT("Banned");
	default:													return TEXT("Unknown");
	}
}

FVoiceChatResult ResultFromVivoxStatus(const VivoxClientApi::VCSStatus& Status)
{
	FVoiceChatResult Result = FVoiceChatResult::CreateSuccess();
	if (Status.IsError())
	{
		switch (Status.GetStatusCode())
		{
		case VX_E_NOT_INITIALIZED:
			Result = VoiceChat::Errors::NotInitialized();
			break;
		case VX_E_ALREADY_LOGGED_OUT:
		case VX_E_NOT_LOGGED_IN:
			Result = VoiceChat::Errors::NotLoggedIn();
			break;
		case VX_E_INVALID_ARGUMENT:
		case VX_E_INVALID_USERNAME_OR_PASSWORD:
		case VX_E_CHANNEL_URI_TOO_LONG:
			Result = VoiceChat::Errors::InvalidArgument();
			break;
		case VX_E_CALL_TERMINATED_NO_ANSWER_LOCAL:
			Result = VoiceChat::Errors::ClientTimeout();
			break;
		case VX_E_RTP_TIMEOUT:
		case VxNetworkHttpTimeout:
			Result = VoiceChat::Errors::ServerTimeout();
			break;
		case VX_E_FAILED_TO_CONNECT_TO_SERVER:
			Result = VoiceChat::Errors::ConnectionFailure();
			break;
		case 10005: // Couldn't resolve proxy name
		case VxNetworkNameResolutionFailed:
			Result = VoiceChat::Errors::DnsFailure();
			break;
		case VX_E_ACCESSTOKEN_INVALID_SIGNATURE:
		case VX_E_ACCESSTOKEN_CLAIMS_MISMATCH:
		case VX_E_ACCESSTOKEN_ISSUER_MISMATCH:
		case VX_E_ACCESSTOKEN_MALFORMED:
			Result = VoiceChat::Errors::CredentialsInvalid();
			break;
		case VX_E_ACCESSTOKEN_ALREADY_USED:
		case VX_E_ACCESSTOKEN_EXPIRED:
			Result = VoiceChat::Errors::CredentialsExpired();
			break;
		case VX_E_ALREADY_INITIALIZED:
			Result = VivoxVoiceChat::Errors::AlreadyInitialized();
			break;
		case VX_E_ALREADY_LOGGED_IN:
			Result = VivoxVoiceChat::Errors::AlreadyLoggedIn();
			break;
		case VX_E_CALL_TERMINATED_KICK:
			Result = VivoxVoiceChat::Errors::KickedFromChannel();
			break;
		case VX_E_NO_EXIST:
			Result = VivoxVoiceChat::Errors::NoExist();
			break;
		case VX_E_MAXIMUM_NUMBER_OF_CALLS_EXCEEEDED:
			Result = VivoxVoiceChat::Errors::MaximumNumberOfCallsExceeded();
			break;
		default:
			// TODO map more vivox statuses to text error codes
			Result = VIVOXVOICECHAT_ERROR(EVoiceChatResult::ImplementationError, FString::FromInt(Status.GetStatusCode()));
			break;
		}

		Result.ErrorDesc = FString::Printf(TEXT("StatusCode=%d StatusString=[%s]"), Status.GetStatusCode(), ANSI_TO_TCHAR(Status.ToString()));
		Result.ErrorNum = Status.GetStatusCode();
	}

	return Result;
}

template<class TDelegate, class... TArgs>
void TriggerCompletionDelegates(TArray<TDelegate>& InOutDelegates, const TArgs&... Args)
{
	TArray<TDelegate> Delegates = MoveTemp(InOutDelegates);
	InOutDelegates.Reset();

	for (TDelegate& Delegate : Delegates)
	{
		Delegate.ExecuteIfBound(Args...);
	}
}

template<class TDelegate, class... TArgs>
void TriggerCompletionDelegate(TDelegate& InOutDelegate, const TArgs&... Args)
{
	TDelegate Delegate = InOutDelegate;
	InOutDelegate.Unbind();
	Delegate.ExecuteIfBound(Args...);
}

VivoxClientApi::Vector ToVivoxVector(const FVector& Vec)
{
	return { Vec.Y, Vec.Z, -Vec.X };
}

bool VivoxNameContainsValidCharacters(const FString& Name)
{
	// Must contain characters chosen only from letters a-z and A-Z, numbers 0-9, and the following characters: =+-_.!~()%
	static const FString AdditionalValidCharacters = TEXT("=+-_.!~()%");
	for (const TCHAR& Char : Name)
	{
		int32 Index;
		if (FChar::IsAlnum(Char) || AdditionalValidCharacters.FindChar(Char, Index))
		{
			continue;
		}

		return false;
	}

	return true;
}

}

FVivoxVoiceChatUser::FVivoxVoiceChatUser(FVivoxVoiceChat& InVivoxVoiceChat)
	: VivoxClientConnection(InVivoxVoiceChat.VivoxClientConnection) 
	, VivoxVoiceChat(InVivoxVoiceChat)
{
}

FVivoxVoiceChatUser::~FVivoxVoiceChatUser()
{
	if (!bReleased)
	{
		VIVOXVOICECHATUSER_LOG(Error, TEXT("~FVivoxVoiceChatUser called via route other than FVivoxVoiceChat::ReleaseUser"));
	}
}

void FVivoxVoiceChatUser::SetSetting(const FString& Name, const FString& Value)
{
}

FString FVivoxVoiceChatUser::GetSetting(const FString& Name)
{
	return FString();
}

void FVivoxVoiceChatUser::SetAudioInputVolume(float InVolume)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("SetAudioInputVolume %f"), InVolume);

	AudioInputOptions.Volume = InVolume;
	ApplyAudioInputOptions();
}

void FVivoxVoiceChatUser::SetAudioOutputVolume(float InVolume)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("SetAudioOutputVolume %f"), InVolume);

	AudioOutputOptions.Volume = InVolume;
	ApplyAudioOutputOptions();
}

float FVivoxVoiceChatUser::GetAudioInputVolume() const
{
	return static_cast<float>(VivoxClientConnection.GetMasterAudioInputDeviceVolume(LoginSession.AccountName) - VIVOX_MIN_VOL) / static_cast<float>(VIVOX_MAX_VOL - VIVOX_MIN_VOL);
}

float FVivoxVoiceChatUser::GetAudioOutputVolume() const
{
	return static_cast<float>(VivoxClientConnection.GetMasterAudioOutputDeviceVolume(LoginSession.AccountName) - VIVOX_MIN_VOL) / static_cast<float>(VIVOX_MAX_VOL - VIVOX_MIN_VOL);
}

void FVivoxVoiceChatUser::SetAudioInputDeviceMuted(bool bIsMuted)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("SetAudioInputDeviceMuted %s"), *LexToString(bIsMuted));

	AudioInputOptions.bMuted = bIsMuted;
	ApplyAudioInputOptions();
}

void FVivoxVoiceChatUser::SetAudioOutputDeviceMuted(bool bIsMuted)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("SetAudioOutputDeviceMuted %s"), *LexToString(bIsMuted));

	AudioOutputOptions.bMuted = bIsMuted;
	ApplyAudioOutputOptions();
}

bool FVivoxVoiceChatUser::GetAudioInputDeviceMuted() const
{
	return AudioInputOptions.bMuted;
}

bool FVivoxVoiceChatUser::GetAudioOutputDeviceMuted() const
{
	return AudioOutputOptions.bMuted;
}

TArray<FVoiceChatDeviceInfo> FVivoxVoiceChatUser::GetAvailableInputDeviceInfos() const
{
	TArray<FVoiceChatDeviceInfo> InputDevices;
	const VivoxClientApi::AudioDeviceId* AudioDevices = nullptr;
	int NumAudioDevices = 0;
	VivoxClientConnection.GetAvailableAudioInputDevices(LoginSession.AccountName, AudioDevices, NumAudioDevices);
	InputDevices.Reserve(NumAudioDevices);
	if (AudioDevices)
	{
		for (int DeviceIndex = 0; DeviceIndex < NumAudioDevices; ++DeviceIndex)
		{
			FVoiceChatDeviceInfo& DeviceInfo = InputDevices.Emplace_GetRef();
			DeviceInfo.DisplayName = UTF8_TO_TCHAR(AudioDevices[DeviceIndex].GetAudioDeviceDisplayName());
			DeviceInfo.Id = UTF8_TO_TCHAR(AudioDevices[DeviceIndex].GetAudioDeviceId());
		}
	}
	return InputDevices;
}

TArray<FVoiceChatDeviceInfo> FVivoxVoiceChatUser::GetAvailableOutputDeviceInfos() const
{
	TArray<FVoiceChatDeviceInfo> OutputDevices;
	const VivoxClientApi::AudioDeviceId* AudioDevices = nullptr;
	int NumAudioDevices = 0;
	VivoxClientConnection.GetAvailableAudioOutputDevices(LoginSession.AccountName, AudioDevices, NumAudioDevices);
	OutputDevices.Reserve(NumAudioDevices);
	if (AudioDevices)
	{
		for (int DeviceIndex = 0; DeviceIndex < NumAudioDevices; ++DeviceIndex)
		{
			FVoiceChatDeviceInfo& DeviceInfo = OutputDevices.Emplace_GetRef();
			DeviceInfo.DisplayName = UTF8_TO_TCHAR(AudioDevices[DeviceIndex].GetAudioDeviceDisplayName());
			DeviceInfo.Id = UTF8_TO_TCHAR(AudioDevices[DeviceIndex].GetAudioDeviceId());
		}
	}
	return OutputDevices;
}

void FVivoxVoiceChatUser::SetInputDeviceId(const FString& InputDeviceId)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("SetInputDeviceId %s"), *InputDeviceId);

	bool bDeviceFound = false;
	if (!InputDeviceId.IsEmpty())
	{
		const VivoxClientApi::AudioDeviceId* AudioDevices = nullptr;
		int NumAudioDevices = 0;
		VivoxClientConnection.GetAvailableAudioInputDevices(LoginSession.AccountName, AudioDevices, NumAudioDevices);
		if (AudioDevices)
		{
			for (int DeviceIndex = 0; DeviceIndex < NumAudioDevices; ++DeviceIndex)
			{
				const VivoxClientApi::AudioDeviceId& DeviceId = AudioDevices[DeviceIndex];
				if (InputDeviceId == UTF8_TO_TCHAR(DeviceId.GetAudioDeviceId()))
				{
					AudioInputOptions.DevicePolicy.SetSpecificAudioDevice(DeviceId);
					bDeviceFound = true;
					break;
				}
			}
		}
	}

	if (!bDeviceFound)
	{
		AudioInputOptions.DevicePolicy.SetUseDefaultAudioDevice();
	}

	ApplyAudioInputDevicePolicy();
}

void FVivoxVoiceChatUser::SetOutputDeviceId(const FString& OutputDeviceId)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("SetOutputDeviceId %s"), *OutputDeviceId);

	bool bDeviceFound = false;
	if (!OutputDeviceId.IsEmpty())
	{
		const VivoxClientApi::AudioDeviceId* AudioDevices = nullptr;
		int NumAudioDevices = 0;
		VivoxClientConnection.GetAvailableAudioOutputDevices(LoginSession.AccountName, AudioDevices, NumAudioDevices);
		if (AudioDevices)
		{
			for (int DeviceIndex = 0; DeviceIndex < NumAudioDevices; ++DeviceIndex)
			{
				const VivoxClientApi::AudioDeviceId& DeviceId = AudioDevices[DeviceIndex];
				if (OutputDeviceId == UTF8_TO_TCHAR(DeviceId.GetAudioDeviceId()))
				{
					AudioOutputOptions.DevicePolicy.SetSpecificAudioDevice(DeviceId);
					bDeviceFound = true;
					break;
				}
			}
		}
	}

	if (!bDeviceFound)
	{
		AudioOutputOptions.DevicePolicy.SetUseDefaultAudioDevice();
	}

	ApplyAudioOutputDevicePolicy();
}

FVoiceChatDeviceInfo FVivoxVoiceChatUser::GetInputDeviceInfo() const
{
	FVoiceChatDeviceInfo DeviceInfo;
	if (AudioInputOptions.DevicePolicy.GetAudioDevicePolicy() == VivoxClientApi::AudioDevicePolicy::vx_audio_device_policy_specific_device)
	{
		VivoxClientApi::AudioDeviceId AudioDevice = AudioInputOptions.DevicePolicy.GetSpecificAudioDevice();
		DeviceInfo.DisplayName = UTF8_TO_TCHAR(AudioDevice.GetAudioDeviceDisplayName());
		DeviceInfo.Id = UTF8_TO_TCHAR(AudioDevice.GetAudioDeviceId());
	}
	else
	{
		DeviceInfo = GetDefaultInputDeviceInfo();
	}
	return DeviceInfo;
}

FVoiceChatDeviceInfo FVivoxVoiceChatUser::GetOutputDeviceInfo() const
{
	FVoiceChatDeviceInfo DeviceInfo;
	if (AudioOutputOptions.DevicePolicy.GetAudioDevicePolicy() == VivoxClientApi::AudioDevicePolicy::vx_audio_device_policy_specific_device)
	{
		VivoxClientApi::AudioDeviceId AudioDevice = AudioOutputOptions.DevicePolicy.GetSpecificAudioDevice();
		DeviceInfo.DisplayName = UTF8_TO_TCHAR(AudioDevice.GetAudioDeviceDisplayName());
		DeviceInfo.Id = UTF8_TO_TCHAR(AudioDevice.GetAudioDeviceId());
	}
	else
	{
		DeviceInfo = GetDefaultOutputDeviceInfo();
	}
	return DeviceInfo;
}

FVoiceChatDeviceInfo FVivoxVoiceChatUser::GetDefaultInputDeviceInfo() const
{
	VivoxClientApi::AudioDeviceId DeviceId = VivoxClientConnection.GetOperatingSystemChosenAudioInputDevice(LoginSession.AccountName);

	FVoiceChatDeviceInfo DeviceInfo;
	DeviceInfo.DisplayName = UTF8_TO_TCHAR(DeviceId.GetAudioDeviceDisplayName());
	DeviceInfo.Id = UTF8_TO_TCHAR(DeviceId.GetAudioDeviceId());
	return DeviceInfo;
}

FVoiceChatDeviceInfo FVivoxVoiceChatUser::GetDefaultOutputDeviceInfo() const
{
	VivoxClientApi::AudioDeviceId DeviceId = VivoxClientConnection.GetOperatingSystemChosenAudioOutputDevice(LoginSession.AccountName);
	
	FVoiceChatDeviceInfo DeviceInfo;
	DeviceInfo.DisplayName = UTF8_TO_TCHAR(DeviceId.GetAudioDeviceDisplayName());
	DeviceInfo.Id = UTF8_TO_TCHAR(DeviceId.GetAudioDeviceId());
	return DeviceInfo;
}

void FVivoxVoiceChatUser::Login(FPlatformUserId PlatformId, const FString& PlayerName, const FString& Credentials, const FOnVoiceChatLoginCompleteDelegate& Delegate)
{
	FVoiceChatResult Result = FVoiceChatResult::CreateSuccess();

	if (!IsInitialized())
	{
		Result = VoiceChat::Errors::NotInitialized();
	}
	else if (!IsConnected())
	{
		Result = VoiceChat::Errors::NotConnected();
	}
	else if (IsLoggedIn())
	{
		if (PlayerName == GetLoggedInPlayerName())
		{
			Delegate.ExecuteIfBound(PlayerName, FVoiceChatResult::CreateSuccess());
			return;
		}
		else
		{
			Result = VoiceChat::Errors::OtherUserLoggedIn();
		}
	}
	else if (PlayerName.IsEmpty())
	{
		Result = VoiceChat::Errors::InvalidArgument(TEXT("PlayerName empty"));
	}
	else if (!VivoxNameContainsValidCharacters(PlayerName))
	{
		Result = VoiceChat::Errors::InvalidArgument(TEXT("PlayerName invalid characters"));
	}
	else if(PlayerName.Len() > 60 - VivoxVoiceChat.VivoxNamespace.Len())
	{
		// Name must be between 3-63 characters long and start and end with a '.'. It also must contain the issuer and another '.' separating issuer and name
		Result = VoiceChat::Errors::InvalidArgument(TEXT("PlayerName too long"));
	}

	if (!Result.IsSuccess())
	{
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("Login PlayerName:%s %s"), *PlayerName, *LexToString(Result));
		Delegate.ExecuteIfBound(PlayerName, Result);
		return;
	}

	const VivoxClientApi::AccountName AccountName = VivoxVoiceChat.CreateAccountName(PlayerName);
	const VivoxClientApi::Uri UserUri = VivoxVoiceChat.CreateUserUri(PlayerName);

	if (!AccountName.IsValid() || !UserUri.IsValid())
	{
		Result = VoiceChat::Errors::CredentialsInvalid();
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("Login PlayerName:%s %s"), *PlayerName, *LexToString(Result));
		Delegate.ExecuteIfBound(PlayerName, Result);
		return;
	}

	LoginSession.PlatformId = PlatformId;
	LoginSession.PlayerName = PlayerName;
	LoginSession.AccountName = AccountName;
	LoginSession.UserUri = UserUri;
	LoginSession.State = FLoginSession::EState::LoggingIn;

	OnVoiceChatLoginCompleteDelegate = Delegate;

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
	VivoxClientApi::VCSStatus Status = VivoxClientConnection.Login(LoginSession.AccountName, TCHAR_TO_ANSI(*Credentials));
	if (Status.IsError())
	{
		Result = ResultFromVivoxStatus(Status);
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("Login account:%s %s"), ANSI_TO_TCHAR(LoginSession.AccountName.ToString()), *LexToString(Result));

		ClearLoginSession();
		TriggerCompletionDelegate(OnVoiceChatLoginCompleteDelegate, PlayerName, Result);
		return;
	}
}

void FVivoxVoiceChatUser::Logout(const FOnVoiceChatLogoutCompleteDelegate& Delegate)
{
	FVoiceChatResult Result = FVoiceChatResult::CreateSuccess();

	if (!IsInitialized())
	{
		Result = VoiceChat::Errors::NotInitialized();
	}
	else if (!IsConnected())
	{
		Result = VoiceChat::Errors::NotConnected();
	}
	else if (!IsLoggedIn())
	{
		Result = VoiceChat::Errors::NotLoggedIn();
	}
	// TODO: handle IsLoggingIn case

	if (!Result.IsSuccess())
	{
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("Logout %s"), *LexToString(Result));
		Delegate.ExecuteIfBound(FString(), Result);
		return;
	}

	OnVoiceChatLogoutCompleteDelegate = Delegate;

	VivoxClientApi::AccountName AccountName = VivoxVoiceChat.CreateAccountName(LoginSession.PlayerName);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
	VivoxClientApi::VCSStatus Status = VivoxClientConnection.Logout(AccountName);
	if (Status.IsError())
	{
		Result = ResultFromVivoxStatus(Status);
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("Logout %s"), *LexToString(Result));
		TriggerCompletionDelegate(OnVoiceChatLogoutCompleteDelegate, LoginSession.PlayerName, Result);
		return;
	}

	LoginSession.State = FLoginSession::EState::LoggingOut;
}

bool FVivoxVoiceChatUser::IsLoggingIn() const
{
	return LoginSession.State == FLoginSession::EState::LoggingIn;
}

bool FVivoxVoiceChatUser::IsLoggedIn() const
{
	return LoginSession.State == FLoginSession::EState::LoggedIn;
}

FString FVivoxVoiceChatUser::GetLoggedInPlayerName() const
{
	return IsLoggedIn() ? LoginSession.PlayerName : FString();
}

void FVivoxVoiceChatUser::BlockPlayers(const TArray<FString>& PlayerNames)
{
	TArray<VivoxClientApi::Uri> UsersToBlock;
	for (const FString& PlayerName : PlayerNames)
	{
		UsersToBlock.Add(VivoxVoiceChat.CreateUserUri(PlayerName));
	}

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
	VivoxClientApi::VCSStatus Status = VivoxClientConnection.BlockUsers(LoginSession.AccountName, UsersToBlock.GetData(), UsersToBlock.Num());
	if (Status.IsError())
	{
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("BlockPlayers failed: error:%s (%i)"), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
	}
	else
	{
		VIVOXVOICECHATUSER_LOG(Log, TEXT("Players blocked: [%s]"), *FString::Join(PlayerNames, TEXT(", ")));
	}
}

void FVivoxVoiceChatUser::UnblockPlayers(const TArray<FString>& PlayerNames)
{
	TArray<VivoxClientApi::Uri> UsersToUnblock;
	for (const FString& PlayerName : PlayerNames)
	{
		UsersToUnblock.Add(VivoxVoiceChat.CreateUserUri(PlayerName));
	}

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
	VivoxClientApi::VCSStatus Status = VivoxClientConnection.UnblockUsers(LoginSession.AccountName, UsersToUnblock.GetData(), UsersToUnblock.Num());
	if (Status.IsError())
	{
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("UnblockPlayers failed: error:%s (%i)"), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
	}
	else
	{
		VIVOXVOICECHATUSER_LOG(Log, TEXT("Players unblocked: [%s]"), *FString::Join(PlayerNames, TEXT(", ")));
	}
}

void FVivoxVoiceChatUser::JoinChannel(const FString& ChannelName, const FString& ChannelCredentials, EVoiceChatChannelType ChannelType, const FOnVoiceChatChannelJoinCompleteDelegate& Delegate, TOptional<FVoiceChatChannel3dProperties> Channel3dProperties)
{
	FVoiceChatResult Result = FVoiceChatResult::CreateSuccess();

	if (!IsInitialized())
	{
		Result = VoiceChat::Errors::NotInitialized();
	}
	else if (!IsConnected())
	{
		Result = VoiceChat::Errors::NotConnected();
	}
	else if (!IsLoggedIn())
	{
		Result = VoiceChat::Errors::NotLoggedIn();
	}
	else if (ChannelName.IsEmpty())
	{
		Result = VoiceChat::Errors::InvalidArgument(TEXT("ChannelName empty"));
	}
	else if (!VivoxNameContainsValidCharacters(ChannelName))
	{
		Result = VoiceChat::Errors::InvalidArgument(TEXT("ChannelName invalid characters"));
	}
	else if (ChannelName.Len() > 189 - VivoxVoiceChat.VivoxNamespace.Len())
	{
		// channel name length must not exceed 200 characters, including the confctl-?- prefix and the issuer and separator
		Result = VoiceChat::Errors::InvalidArgument(TEXT("ChannelName too long"));
	}

	if (!Result.IsSuccess())
	{
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("JoinChannel ChannelName:%s %s"), *ChannelName, *LexToString(Result));
		Delegate.ExecuteIfBound(ChannelName, Result);
		return;
	}

	FChannelSession& ChannelSession = GetChannelSession(ChannelName);
	if (ChannelSession.State == FChannelSession::EState::Connected)
	{
		Delegate.ExecuteIfBound(ChannelName, FVoiceChatResult::CreateSuccess());
		return;
	}
	else if (ChannelSession.State == FChannelSession::EState::Connecting)
	{
		Delegate.ExecuteIfBound(ChannelName, VoiceChat::Errors::ChannelJoinInProgress());
		return;
	}
	else if (ChannelSession.State == FChannelSession::EState::Disconnecting)
	{
		Delegate.ExecuteIfBound(ChannelName, VoiceChat::Errors::ChannelLeaveInProgress());
		return;
	}

	ChannelSession.ChannelName = ChannelName;
	ChannelSession.ChannelType = ChannelType;
	ChannelSession.ChannelUri = VivoxVoiceChat.CreateChannelUri(ChannelName, ChannelType, Channel3dProperties);
	ChannelSession.JoinDelegate = Delegate;

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
	VivoxClientApi::VCSStatus Status = VivoxClientConnection.JoinChannel(LoginSession.AccountName, ChannelSession.ChannelUri, TCHAR_TO_ANSI(*ChannelCredentials));
	if (Status.IsError())
	{
		Result = ResultFromVivoxStatus(Status);
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("JoinChannel ChannelUri:%s %s"), ANSI_TO_TCHAR(ChannelSession.ChannelUri.ToString()), *LexToString(Result));
		TriggerCompletionDelegate(ChannelSession.JoinDelegate, ChannelName, Result);
		return;
	}

	ChannelSession.State = FChannelSession::EState::Connecting;
}

void FVivoxVoiceChatUser::LeaveChannel(const FString& ChannelName, const FOnVoiceChatChannelLeaveCompleteDelegate& Delegate)
{
	FVoiceChatResult Result = FVoiceChatResult::CreateSuccess();

	if (!IsInitialized())
	{
		Result = VoiceChat::Errors::NotInitialized();
	}
	else if (!IsConnected())
	{
		Result = VoiceChat::Errors::NotConnected();
	}
	else if (!IsLoggedIn())
	{
		Result = VoiceChat::Errors::NotLoggedIn();
	}
	else if (ChannelName.IsEmpty())
	{
		Result = VoiceChat::Errors::InvalidArgument();
	}

	FChannelSession& ChannelSession = GetChannelSession(ChannelName);
	if (ChannelSession.State != FChannelSession::EState::Connected)
	{
		Result = VoiceChat::Errors::NotInChannel();
	}

	if (!Result.IsSuccess())
	{
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("LeaveChannel ChannelName:%s %s"), *ChannelName, *LexToString(Result));
		Delegate.ExecuteIfBound(ChannelName, Result);
		return;
	}

	ChannelSession.LeaveDelegate = Delegate;

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
	VivoxClientApi::VCSStatus Status = VivoxClientConnection.LeaveChannel(LoginSession.AccountName, ChannelSession.ChannelUri);
	if (Status.IsError())
	{
		Result = ResultFromVivoxStatus(Status);
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("LeaveChannel channel:%s %s"), ANSI_TO_TCHAR(ChannelSession.ChannelUri.ToString()), *LexToString(Result));
		TriggerCompletionDelegate(ChannelSession.LeaveDelegate, ChannelName, Result);
		return;
	}

	ChannelSession.State = FChannelSession::EState::Disconnecting;
}

void FVivoxVoiceChatUser::Set3DPosition(const FString& ChannelName, const FVector& SpeakerPosition, const FVector& ListenerPosition, const FVector& ListenerForwardDirection, const FVector& ListenerUpDirection)
{
	FChannelSession& ChannelSession = GetChannelSession(ChannelName);

	// Transform Pos and Direction to up -> (0,1,0) and left -> (-1, 0, 0)
	FVector RotatedPos(ListenerPosition.Y, ListenerPosition.Z, -ListenerPosition.X);
	FVector RotatedForwardDirection(ListenerForwardDirection.Y, ListenerForwardDirection.Z, -ListenerForwardDirection.X);
	FVector RotatedUpDirection(ListenerUpDirection.Y, ListenerUpDirection.Z, -ListenerUpDirection.X);

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
	VivoxClientApi::VCSStatus Status = VivoxClientConnection.Set3DPosition(LoginSession.AccountName, ChannelSession.ChannelUri, ToVivoxVector(SpeakerPosition), ToVivoxVector(ListenerPosition), ToVivoxVector(ListenerForwardDirection), ToVivoxVector(ListenerUpDirection));
	if (Status.IsError())
	{
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("Set3DPosition failed: channel:%s error:%s (%i)"), ANSI_TO_TCHAR(ChannelSession.ChannelUri.ToString()), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
	}
}

TArray<FString> FVivoxVoiceChatUser::GetChannels() const
{
	TArray<FString> ChannelNames;
	for (const TPair<FString, FChannelSession>& ChannelSessionPair : LoginSession.ChannelSessions)
	{
		const FString& ChannelName = ChannelSessionPair.Key;
		const FChannelSession& ChannelSession = ChannelSessionPair.Value;
		if (ChannelSession.State == FChannelSession::EState::Connected)
		{
			ChannelNames.Add(ChannelName);
		}
	}
	return ChannelNames;
}

TArray<FString> FVivoxVoiceChatUser::GetPlayersInChannel(const FString& ChannelName) const
{
	TArray<FString> PlayerNames;
	GetChannelSession(ChannelName).Participants.GenerateKeyArray(PlayerNames);
	return PlayerNames;
}

EVoiceChatChannelType FVivoxVoiceChatUser::GetChannelType(const FString& ChannelName) const
{
	return GetChannelSession(ChannelName).ChannelType;
}

bool FVivoxVoiceChatUser::IsPlayerTalking(const FString& PlayerName) const
{
	return GetParticipant(PlayerName).bTalking;
}

void FVivoxVoiceChatUser::SetPlayerMuted(const FString& PlayerName, bool bMuted)
{
	FParticipant& Participant = GetParticipant(PlayerName);
	Participant.bMuted = bMuted;

	for (TPair<FString, FChannelSession>& ChannelSessionPair : LoginSession.ChannelSessions)
	{
		FChannelSession& ChannelSession = ChannelSessionPair.Value;
		if (ChannelSession.State == FChannelSession::EState::Connected)
		{
			if (FParticipant* ChannelParticipant = ChannelSession.Participants.Find(PlayerName))
			{
				const bool bShouldMute = Participant.bMuted || Participant.Volume < SMALL_NUMBER;
				CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
				VivoxClientApi::VCSStatus Status = VivoxClientConnection.SetParticipantMutedForMe(LoginSession.AccountName, ChannelParticipant->UserUri, ChannelSession.ChannelUri, bShouldMute);
				if (Status.IsError())
				{
					VIVOXVOICECHATUSER_LOG(Warning, TEXT("SetParticipantMutedForMe failed: channel:%s user:%s muted:%s error:%s (%i)"), ANSI_TO_TCHAR(ChannelSession.ChannelUri.ToString()), ANSI_TO_TCHAR(ChannelParticipant->UserUri.ToString()), *LexToString(bShouldMute), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
					// TODO: This will fail only when Account/participant/channel is not found -> fixup our state
				}
			}
		}
	}
}

bool FVivoxVoiceChatUser::IsPlayerMuted(const FString& PlayerName) const
{
	return GetParticipant(PlayerName).bMuted;
}

void FVivoxVoiceChatUser::SetPlayerVolume(const FString& PlayerName, float Volume)
{
	FParticipant& Participant = GetParticipant(PlayerName);
	Participant.Volume = FMath::Clamp(Volume, 0.0f, 1.0f);
	Participant.IntVolume = FMath::Lerp(VIVOX_MIN_VOL, VIVOX_MAX_VOL, Participant.Volume);

	for (TPair<FString, FChannelSession>& ChannelSessionPair : LoginSession.ChannelSessions)
	{
		FChannelSession& ChannelSession = ChannelSessionPair.Value;
		if (ChannelSession.State == FChannelSession::EState::Connected)
		{
			if (FParticipant* ChannelParticipant = ChannelSession.Participants.Find(PlayerName))
			{
				const bool bShouldMute = Participant.bMuted || Participant.Volume < SMALL_NUMBER;
				CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
				VivoxClientApi::VCSStatus Status = VivoxClientConnection.SetParticipantMutedForMe(LoginSession.AccountName, ChannelParticipant->UserUri, ChannelSession.ChannelUri, bShouldMute);
				if (Status.IsError())
				{
					VIVOXVOICECHATUSER_LOG(Warning, TEXT("SetParticipantMutedForMe failed: channel:%s user:%s muted:%s error:%s (%i)"), ANSI_TO_TCHAR(ChannelSession.ChannelUri.ToString()), ANSI_TO_TCHAR(ChannelParticipant->UserUri.ToString()), *LexToString(bShouldMute), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
					// TODO: This will fail only when Account/participant/channel is not found -> fixup our state
				}

				Status = VivoxClientConnection.SetParticipantAudioOutputDeviceVolumeForMe(LoginSession.AccountName, ChannelParticipant->UserUri, ChannelSession.ChannelUri, Participant.IntVolume);
				if (Status.IsError())
				{
					VIVOXVOICECHATUSER_LOG(Warning, TEXT("SetParticipantAudioOutputDeviceVolumeForMe failed: channel:%s user:%s volume:%i error:%s (%i)"), ANSI_TO_TCHAR(ChannelSession.ChannelUri.ToString()), ANSI_TO_TCHAR(ChannelParticipant->UserUri.ToString()), Participant.IntVolume, ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
					// TODO: This will fail only when Account/participant/channel is not found -> fixup our state
				}
			}
		}
	}
}

float FVivoxVoiceChatUser::GetPlayerVolume(const FString& PlayerName) const
{
	return GetParticipant(PlayerName).Volume;
}

void FVivoxVoiceChatUser::TransmitToNoChannels()
{
	VIVOXVOICECHATUSER_LOG(Log, TEXT("TransmitToNoChannels"));

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
	VivoxClientApi::VCSStatus Status = VivoxClientConnection.SetTransmissionToNone(LoginSession.AccountName);
	if (Status.IsError())
	{
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("SetTransmissionToNone failed: error:%s (%i)"), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
	}
}

void FVivoxVoiceChatUser::TransmitToAllChannels()
{
	VIVOXVOICECHATUSER_LOG(Log, TEXT("TransmitToAllChannels"));

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
	VivoxClientApi::VCSStatus Status = VivoxClientConnection.SetTransmissionToAll(LoginSession.AccountName);
	if (Status.IsError())
	{
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("SetTransmissionToAll failed: error:%s (%i)"), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
	}
}

void FVivoxVoiceChatUser::TransmitToSpecificChannel(const FString& Channel)
{
	VIVOXVOICECHATUSER_LOG(Log, TEXT("TransmitToSpecificChannel %s"), *Channel);

	FChannelSession& ChannelSession = GetChannelSession(Channel);
	if (ChannelSession.State == FChannelSession::EState::Connected)
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
		VivoxClientApi::VCSStatus Status = VivoxClientConnection.SetTransmissionToSpecificChannel(LoginSession.AccountName, ChannelSession.ChannelUri);
		if (Status.IsError())
		{
			VIVOXVOICECHATUSER_LOG(Warning, TEXT("TransmitToSpecificChannel failed: channel:%s error:%s (%i)"), ANSI_TO_TCHAR(ChannelSession.ChannelUri.ToString()), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
		}
	}
}

EVoiceChatTransmitMode FVivoxVoiceChatUser::GetTransmitMode() const
{
	VivoxClientApi::ChannelTransmissionPolicy TransmissionPolicy = VivoxClientConnection.GetChannelTransmissionPolicy(LoginSession.AccountName);
	switch (TransmissionPolicy.GetChannelTransmissionPolicy())
	{
	default:
	case VivoxClientApi::ChannelTransmissionPolicy::vx_channel_transmission_policy_all:
		return EVoiceChatTransmitMode::All;
	case VivoxClientApi::ChannelTransmissionPolicy::vx_channel_transmission_policy_none:
		return EVoiceChatTransmitMode::None;
	case VivoxClientApi::ChannelTransmissionPolicy::vx_channel_transmission_policy_specific_channel:
		return EVoiceChatTransmitMode::Channel;
	}
}

FString FVivoxVoiceChatUser::GetTransmitChannel() const
{
	VivoxClientApi::ChannelTransmissionPolicy TransmissionPolicy = VivoxClientConnection.GetChannelTransmissionPolicy(LoginSession.AccountName);
	if (TransmissionPolicy.GetChannelTransmissionPolicy() == VivoxClientApi::ChannelTransmissionPolicy::vx_channel_transmission_policy_specific_channel)
	{
		return ANSI_TO_TCHAR(TransmissionPolicy.GetSpecificTransmissionChannel().ToString());
	}
	else
	{
		return FString();
	}
}

FDelegateHandle FVivoxVoiceChatUser::StartRecording(const FOnVoiceChatRecordSamplesAvailableDelegate::FDelegate& Delegate)
{
	if (!VivoxClientConnection.AudioInputDeviceTestIsRecording())
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
		VivoxClientApi::VCSStatus Status = VivoxClientConnection.StartAudioInputDeviceTestRecord();
		if (Status.IsError())
		{
			VIVOXVOICECHATUSER_LOG(Warning, TEXT("StartRecording failed: error:%s (%i)"), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
			return FDelegateHandle();
		}
	}

	FScopeLock Lock(&AudioRecordLock);

	return OnVoiceChatRecordSamplesAvailableDelegate.Add(Delegate);
}

void FVivoxVoiceChatUser::StopRecording(FDelegateHandle Handle)
{
	FScopeLock Lock(&AudioRecordLock);

	OnVoiceChatRecordSamplesAvailableDelegate.Remove(Handle);

	if (!OnVoiceChatRecordSamplesAvailableDelegate.IsBound())
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
		VivoxClientConnection.StopAudioInputDeviceTestRecord();
	}
}

FDelegateHandle FVivoxVoiceChatUser::RegisterOnVoiceChatAfterCaptureAudioReadDelegate(const FOnVoiceChatAfterCaptureAudioReadDelegate::FDelegate& Delegate)
{
	FScopeLock Lock(&AfterCaptureAudioReadLock);

	return OnVoiceChatAfterCaptureAudioReadDelegate.Add(Delegate);
}

void FVivoxVoiceChatUser::UnregisterOnVoiceChatAfterCaptureAudioReadDelegate(FDelegateHandle Handle)
{
	FScopeLock Lock(&AfterCaptureAudioReadLock);

	OnVoiceChatAfterCaptureAudioReadDelegate.Remove(Handle);
}

FDelegateHandle FVivoxVoiceChatUser::RegisterOnVoiceChatBeforeCaptureAudioSentDelegate(const FOnVoiceChatBeforeCaptureAudioSentDelegate::FDelegate& Delegate)
{
	FScopeLock Lock(&BeforeCaptureAudioSentLock);

	return OnVoiceChatBeforeCaptureAudioSentDelegate.Add(Delegate);
}

void FVivoxVoiceChatUser::UnregisterOnVoiceChatBeforeCaptureAudioSentDelegate(FDelegateHandle Handle)
{
	FScopeLock Lock(&BeforeCaptureAudioSentLock);

	OnVoiceChatBeforeCaptureAudioSentDelegate.Remove(Handle);
}

FDelegateHandle FVivoxVoiceChatUser::RegisterOnVoiceChatBeforeRecvAudioRenderedDelegate(const FOnVoiceChatBeforeRecvAudioRenderedDelegate::FDelegate& Delegate)
{
	FScopeLock Lock(&BeforeRecvAudioRenderedLock);

	return OnVoiceChatBeforeRecvAudioRenderedDelegate.Add(Delegate);
}

void FVivoxVoiceChatUser::UnregisterOnVoiceChatBeforeRecvAudioRenderedDelegate(FDelegateHandle Handle)
{
	FScopeLock Lock(&BeforeRecvAudioRenderedLock);

	OnVoiceChatBeforeRecvAudioRenderedDelegate.Remove(Handle);
}

FString FVivoxVoiceChatUser::InsecureGetLoginToken(const FString& PlayerName)
{
	FString Token;

	if (IsInitialized())
	{
		VivoxClientApi::Uri UserUri = VivoxVoiceChat.CreateUserUri(PlayerName);
		if (char* ANSIToken = vx_debug_generate_token(TCHAR_TO_ANSI(*VivoxVoiceChat.VivoxIssuer), FDateTime::UtcNow().ToUnixTimestamp() + 90, "login", FMath::Rand(), nullptr, UserUri.ToString(), nullptr, (const unsigned char*)TCHAR_TO_ANSI(*VivoxVoiceChat.VivoxInsecureSecret), VivoxVoiceChat.VivoxInsecureSecret.Len()))
		{
			Token = ANSI_TO_TCHAR(ANSIToken);
			vx_free(ANSIToken);
		}
	}

	return Token;
}

FString FVivoxVoiceChatUser::InsecureGetJoinToken(const FString& ChannelName, EVoiceChatChannelType ChannelType, TOptional<FVoiceChatChannel3dProperties> Channel3dProperties)
{
	FString Token;

	if (IsInitialized() && IsLoggedIn())
	{
		VivoxClientApi::Uri ChannelUri = VivoxVoiceChat.CreateChannelUri(ChannelName, ChannelType, Channel3dProperties);
		if (char* ANSIToken = vx_debug_generate_token(TCHAR_TO_ANSI(*VivoxVoiceChat.VivoxIssuer), FDateTime::UtcNow().ToUnixTimestamp() + 90, "join", FMath::Rand(), nullptr, LoginSession.UserUri.ToString(), ChannelUri.ToString(), (const unsigned char*)TCHAR_TO_ANSI(*VivoxVoiceChat.VivoxInsecureSecret), VivoxVoiceChat.VivoxInsecureSecret.Len()))
		{
			Token = ANSI_TO_TCHAR(ANSIToken);
			vx_free(ANSIToken);
		}
	}

	return Token;
}

FString GetLogLevelString(VivoxClientApi::IClientApiEventHandler::LogLevel Level)
{
	switch (Level)
	{
	case VivoxClientApi::IClientApiEventHandler::LogLevel::LogLevelError:		return TEXT("Error");
	case VivoxClientApi::IClientApiEventHandler::LogLevel::LogLevelWarning:		return TEXT("Warning");
	case VivoxClientApi::IClientApiEventHandler::LogLevel::LogLevelInfo:		return TEXT("Info");
	case VivoxClientApi::IClientApiEventHandler::LogLevel::LogLevelDebug:		return TEXT("Debug");
	case VivoxClientApi::IClientApiEventHandler::LogLevel::LogLevelTrace:		return TEXT("Trace");
	default:																	return TEXT("Unknown");
	}
}

bool FVivoxVoiceChatUser::IsInitialized()
{
	return VivoxVoiceChat.IsInitialized();
}

bool FVivoxVoiceChatUser::IsConnected()
{
	return VivoxVoiceChat.IsConnected();
}

bool FVivoxVoiceChatUser::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
#if NO_LOGGING
#define VIVOX_EXEC_LOG(...)
#else
#define VIVOX_EXEC_LOG(Fmt, ...) Ar.CategorizedLogf(LogVivoxVoiceChat.GetCategoryName(), ELogVerbosity::Log, Fmt, ##__VA_ARGS__)
#endif

	if (FParse::Command(&Cmd, TEXT("INFO")))
	{
		if (IsInitialized())
		{
			VIVOX_EXEC_LOG(TEXT("  Input Devices: muted:%s volume:%.2f"), *LexToString(GetAudioInputDeviceMuted()), GetAudioInputVolume());
			const FVoiceChatDeviceInfo InputDevice = GetInputDeviceInfo();
			const FVoiceChatDeviceInfo DefaultInputDevice = GetDefaultInputDeviceInfo();
			if (InputDevice == DefaultInputDevice)
			{
				VIVOX_EXEC_LOG(TEXT("    [%s] (Selected) (Default)"), *LexToString(DefaultInputDevice));
			}
			else
			{
				VIVOX_EXEC_LOG(TEXT("    [%s] (Selected)"), *LexToString(InputDevice));
				VIVOX_EXEC_LOG(TEXT("    [%s] (Default)"), *LexToString(DefaultInputDevice));
			}
			for (const FVoiceChatDeviceInfo& Device : GetAvailableInputDeviceInfos())
			{
				if (Device != DefaultInputDevice && Device != InputDevice)
				{
					VIVOX_EXEC_LOG(TEXT("    [%s]"), *LexToString(Device));
				}
			}

			VIVOX_EXEC_LOG(TEXT("  Output Devices: muted:%s volume:%.2f"), *LexToString(GetAudioOutputDeviceMuted()), GetAudioOutputVolume());
			const FVoiceChatDeviceInfo OutputDevice = GetOutputDeviceInfo();
			const FVoiceChatDeviceInfo DefaultOutputDevice = GetDefaultOutputDeviceInfo();
			if (OutputDevice == DefaultOutputDevice)
			{
				VIVOX_EXEC_LOG(TEXT("    [%s] (Selected) (Default)"), *LexToString(DefaultOutputDevice));
			}
			else
			{
				VIVOX_EXEC_LOG(TEXT("    [%s] (Selected)"), *LexToString(OutputDevice));
				VIVOX_EXEC_LOG(TEXT("    [%s] (Default)"), *LexToString(DefaultOutputDevice));
			}
			for (const FVoiceChatDeviceInfo& Device : GetAvailableOutputDeviceInfos())
			{
				if (Device != DefaultOutputDevice && Device != OutputDevice)
				{
					VIVOX_EXEC_LOG(TEXT("    [%s]"), *LexToString(Device));
				}
			}

			if (IsConnected())
			{
				VIVOX_EXEC_LOG(TEXT("Login Status: %s"), *ToString(LoginSession.State));
				if (IsLoggedIn())
				{
					VIVOX_EXEC_LOG(TEXT("  PlayerName: %s"), *LoginSession.PlayerName);
					VIVOX_EXEC_LOG(TEXT("  AccountName: %s"), ANSI_TO_TCHAR(LoginSession.AccountName.ToString()));
					VIVOX_EXEC_LOG(TEXT("  UserUri: %s"), ANSI_TO_TCHAR(LoginSession.UserUri.ToString()));

					VivoxClientApi::ChannelTransmissionPolicy TransmissionPolicy = VivoxClientConnection.GetChannelTransmissionPolicy(LoginSession.AccountName);
					FString TransmitString;
					switch (GetTransmitMode())
					{
					case EVoiceChatTransmitMode::All:
						TransmitString = TEXT("ALL");
						break;
					case EVoiceChatTransmitMode::None:
						TransmitString = TEXT("NONE");
						break;
					case EVoiceChatTransmitMode::Channel:
						TransmitString = FString::Printf(TEXT("CHANNEL:%s"), *GetTransmitChannel());
						break;
					}
					VIVOX_EXEC_LOG(TEXT("Channels: transmitting:%s"), *TransmitString);
					for (const TPair<FString, FChannelSession>& ChannelSessionPair : LoginSession.ChannelSessions)
					{
						const FString& ChannelName = ChannelSessionPair.Key;
						const FChannelSession& ChannelSession = ChannelSessionPair.Value;
						VIVOX_EXEC_LOG(TEXT("  %s"), *ChannelName);
						VIVOX_EXEC_LOG(TEXT("    Channel Status: %s"), *ToString(ChannelSession.State));
						VIVOX_EXEC_LOG(TEXT("    Channel Uri: %s"), ANSI_TO_TCHAR(ChannelSession.ChannelUri.ToString()));
						VIVOX_EXEC_LOG(TEXT("    Participants:"));
						for (const TPair<FString, FParticipant>& ParticipantPair : ChannelSession.Participants)
						{
							const FString& ParticipantName = ParticipantPair.Key;
							const FParticipant& Participant = ParticipantPair.Value;
							VIVOX_EXEC_LOG(TEXT("      %s uri:%s talking:%s muted:%s volume:%.2f"), *ParticipantName, ANSI_TO_TCHAR(Participant.UserUri.ToString()), *LexToString(Participant.bTalking), *LexToString(Participant.bMuted), Participant.Volume);
						}
					}
				}
			}
		}
		return true;
	}
#if !UE_BUILD_SHIPPING
	else if (FParse::Command(&Cmd, TEXT("INPUT")))
	{
		if (FParse::Command(&Cmd, TEXT("SETVOLUME")))
		{
			FString Volume;
			if (FParse::Token(Cmd, Volume, false))
			{
				SetAudioInputVolume(FCString::Atof(*Volume));
				return true;
			}
		}
		else if (FParse::Command(&Cmd, TEXT("MUTE")))
		{
			SetAudioInputDeviceMuted(true);
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("UNMUTE")))
		{
			SetAudioInputDeviceMuted(false);
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("LISTDEVICES")))
		{
			VIVOX_EXEC_LOG(TEXT("Input Devices:"));
			for (const FVoiceChatDeviceInfo& DeviceInfo : GetAvailableInputDeviceInfos())
			{
				VIVOX_EXEC_LOG(TEXT("  %s"), *LexToString(DeviceInfo));
			}
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("SETDEVICE")))
		{
			FString DeviceId;
			if (FParse::Token(Cmd, DeviceId, false))
			{
				SetInputDeviceId(DeviceId);
				return true;
			}
		}
		else if (FParse::Command(&Cmd, TEXT("SETDEFAULTDEVICE")))
		{
			SetInputDeviceId(FString());
			return true;
		}
	}
	else if (FParse::Command(&Cmd, TEXT("OUTPUT")))
	{
		if (FParse::Command(&Cmd, TEXT("SETVOLUME")))
		{
			FString Volume;
			if (FParse::Token(Cmd, Volume, false))
			{
				SetAudioOutputVolume(FCString::Atof(*Volume));
				return true;
			}
		}
		else if (FParse::Command(&Cmd, TEXT("MUTE")))
		{
			SetAudioOutputDeviceMuted(true);
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("UNMUTE")))
		{
			SetAudioOutputDeviceMuted(false);
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("LISTDEVICES")))
		{
			VIVOX_EXEC_LOG(TEXT("Output Devices:"));
			for (const FVoiceChatDeviceInfo& DeviceInfo : GetAvailableOutputDeviceInfos())
			{
				VIVOX_EXEC_LOG(TEXT("  %s"), *LexToString(DeviceInfo));
			}
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("SETDEVICE")))
		{
			FString DeviceId;
			if (FParse::Token(Cmd, DeviceId, false))
			{
				SetOutputDeviceId(DeviceId);
				return true;
			}
		}
		else if (FParse::Command(&Cmd, TEXT("SETDEFAULTDEVICE")))
		{
			SetOutputDeviceId(FString());
			return true;
		}
	}
	else if (FParse::Command(&Cmd, TEXT("LOGIN")))
	{
		FString PlayerName;
		if (FParse::Token(Cmd, PlayerName, false))
		{
			FString Token = InsecureGetLoginToken(PlayerName);

			Login(0, PlayerName, Token, FOnVoiceChatLoginCompleteDelegate::CreateLambda([](const FString& LoggedInPlayerName, const FVoiceChatResult& Result)
			{
				UE_LOG(LogVivoxVoiceChat, Log, TEXT("VIVOX LOGIN playername:%s result:%s"), *LoggedInPlayerName, *LexToString(Result));
			}));
			return true;
		}
	}
	else if (FParse::Command(&Cmd, TEXT("LOGOUT")))
	{
		Logout(FOnVoiceChatLogoutCompleteDelegate::CreateLambda([](const FString& PlayerName, const FVoiceChatResult& Result)
		{
			UE_LOG(LogVivoxVoiceChat, Log, TEXT("VIVOX LOGOUT playername:%s result:%s"), *PlayerName, *LexToString(Result));
		}));
		return true;
	}
	else if (FParse::Command(&Cmd, TEXT("CHANNEL")))
	{
		if (FParse::Command(&Cmd, TEXT("JOIN")))
		{
			FString ChannelName;
			if (FParse::Token(Cmd, ChannelName, false))
			{
				FString ChannelTypeString;
				EVoiceChatChannelType ChannelType = EVoiceChatChannelType::NonPositional;
				TOptional<FVoiceChatChannel3dProperties> Channel3dProperties;
				if (FParse::Token(Cmd, ChannelTypeString, false))
				{
					if (ChannelTypeString == TEXT("POSITIONAL"))
					{
						ChannelType = EVoiceChatChannelType::Positional;
					}
					else if (ChannelTypeString == TEXT("ECHO"))
					{
						ChannelType = EVoiceChatChannelType::Echo;
					}
				}

				FString Token = InsecureGetJoinToken(ChannelName, ChannelType, Channel3dProperties);

				JoinChannel(ChannelName, Token, ChannelType, FOnVoiceChatChannelJoinCompleteDelegate::CreateLambda([](const FString& JoinedChannelName, const FVoiceChatResult& Result)
				{
					UE_LOG(LogVivoxVoiceChat, Log, TEXT("VIVOX CHANNEL JOIN channelname:%s result:%s"), *JoinedChannelName, *LexToString(Result));
				}), Channel3dProperties);
				return true;
			}
		}
		else if (FParse::Command(&Cmd, TEXT("LEAVE")))
		{
			FString ChannelName;
			if (FParse::Token(Cmd, ChannelName, false))
			{
				LeaveChannel(ChannelName, FOnVoiceChatChannelLeaveCompleteDelegate::CreateLambda([](const FString& LeftChannelName, const FVoiceChatResult& Result)
				{
					UE_LOG(LogVivoxVoiceChat, Log, TEXT("VIVOX CHANNEL LEAVE channelname:%s result:%s"), *LeftChannelName, *LexToString(Result));
				}));
				return true;
			}
		}
		else if (FParse::Command(&Cmd, TEXT("TRANSMIT")))
		{
			FString ChannelName;
			if (FParse::Token(Cmd, ChannelName, false))
			{
				TransmitToSpecificChannel(ChannelName);
				return true;
			}
		}
		else if (FParse::Command(&Cmd, TEXT("TRANSMITALL")))
		{
			TransmitToAllChannels();
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("TRANSMITNONE")))
		{
			TransmitToNoChannels();
			return true;
		}
	}
	else if (FParse::Command(&Cmd, TEXT("PLAYER")))
	{
		if (FParse::Command(&Cmd, TEXT("MUTE")))
		{
			FString PlayerName;
			if (FParse::Token(Cmd, PlayerName, false))
			{
				SetPlayerMuted(PlayerName, true);
				return true;
			}
		}
		else if (FParse::Command(&Cmd, TEXT("UNMUTE")))
		{
			FString PlayerName;
			if (FParse::Token(Cmd, PlayerName, false))
			{
				SetPlayerMuted(PlayerName, false);
				return true;
			}
		}
		else if (FParse::Command(&Cmd, TEXT("SETVOLUME")))
		{
			FString PlayerName;
			if (FParse::Token(Cmd, PlayerName, false))
			{
				FString Volume;
				if (FParse::Token(Cmd, Volume, false))
				{
					SetPlayerVolume(PlayerName, FCString::Atof(*Volume));
					return true;
				}
			}
		}
		else if (FParse::Command(&Cmd, TEXT("BLOCK")))
		{
			TArray<FString> PlayerNames;
			FString PlayerName;
			while (FParse::Token(Cmd, PlayerName, false))
			{
				PlayerNames.Add(PlayerName);
			}
			if (PlayerNames.Num() > 0)
			{
				BlockPlayers(PlayerNames);
				return true;
			}
		}
		else if (FParse::Command(&Cmd, TEXT("UNBLOCK")))
		{
			TArray<FString> PlayerNames;
			FString PlayerName;
			while (FParse::Token(Cmd, PlayerName, false))
			{
				PlayerNames.Add(PlayerName);
			}
			if (PlayerNames.Num() > 0)
			{
				UnblockPlayers(PlayerNames);
				return true;
			}
		}
	}
#endif // !UE_BUILD_SHIPPING

#undef VIVOX_EXEC_LOG

	return false;
}

void FVivoxVoiceChatUser::onConnectCompleted(const VivoxClientApi::Uri& Server)
{
}

void FVivoxVoiceChatUser::onConnectFailed(const VivoxClientApi::Uri& Server, const VivoxClientApi::VCSStatus& Status)
{
}

void FVivoxVoiceChatUser::onDisconnected(const VivoxClientApi::Uri& Server, const VivoxClientApi::VCSStatus& Status)
{
	ClearLoginSession();
}

void FVivoxVoiceChatUser::onLoginCompleted(const VivoxClientApi::AccountName& AccountName)
{
	VIVOXVOICECHATUSER_LOG(Log, TEXT("onLoginCompleted account:%s"), ANSI_TO_TCHAR(AccountName.ToString()));

	FString PlayerName = VivoxVoiceChat.GetPlayerNameFromAccountName(AccountName);

	LoginSession.State = FLoginSession::EState::LoggedIn;

	TriggerCompletionDelegate(OnVoiceChatLoginCompleteDelegate, PlayerName, FVoiceChatResult::CreateSuccess());

	OnVoiceChatLoggedInDelegate.Broadcast(PlayerName);

	ApplyAudioInputOptions();
	ApplyAudioInputDevicePolicy();
	ApplyAudioOutputOptions();
	ApplyAudioOutputDevicePolicy();
}

void FVivoxVoiceChatUser::onInvalidLoginCredentials(const VivoxClientApi::AccountName& AccountName)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onInvalidLoginCredentials account:%s"), ANSI_TO_TCHAR(AccountName.ToString()));

	ClearLoginSession();

	FString PlayerName = VivoxVoiceChat.GetPlayerNameFromAccountName(AccountName);

	TriggerCompletionDelegate(OnVoiceChatLoginCompleteDelegate, PlayerName, VoiceChat::Errors::CredentialsInvalid());
}

void FVivoxVoiceChatUser::onLoginFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onLoginFailed account:%s error:%s (%i)"), ANSI_TO_TCHAR(AccountName.ToString()), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());

	ClearLoginSession();

	FString PlayerName = VivoxVoiceChat.GetPlayerNameFromAccountName(AccountName);

	TriggerCompletionDelegate(OnVoiceChatLoginCompleteDelegate, PlayerName, ResultFromVivoxStatus(Status));
}

void FVivoxVoiceChatUser::onLogoutCompleted(const VivoxClientApi::AccountName& AccountName)
{
	VIVOXVOICECHATUSER_LOG(Log, TEXT("onLogoutCompleted account:%s"), ANSI_TO_TCHAR(AccountName.ToString()));

	ClearLoginSession();

	FString PlayerName = VivoxVoiceChat.GetPlayerNameFromAccountName(AccountName);
	TriggerCompletionDelegate(OnVoiceChatLogoutCompleteDelegate, PlayerName, FVoiceChatResult::CreateSuccess());

	OnVoiceChatLoggedOutDelegate.Broadcast(PlayerName);
}

void FVivoxVoiceChatUser::onLogoutFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onLogoutFailed account:%s error:%s (%i)"), ANSI_TO_TCHAR(AccountName.ToString()), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());

	LoginSession.State = FLoginSession::EState::LoggedIn;

	FString PlayerName = VivoxVoiceChat.GetPlayerNameFromAccountName(AccountName);
	TriggerCompletionDelegate(OnVoiceChatLogoutCompleteDelegate, PlayerName, ResultFromVivoxStatus(Status));
}

void FVivoxVoiceChatUser::onSessionGroupCreated(const VivoxClientApi::AccountName& AccountName, const char* SessionGroupHandle)
{
	SessionGroup = ANSI_TO_TCHAR(SessionGroupHandle);
}

void FVivoxVoiceChatUser::onChannelJoined(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri)
{
	VIVOXVOICECHATUSER_LOG(Log, TEXT("onChannelJoined channel:%s"), ANSI_TO_TCHAR(ChannelUri.ToString()));

	FChannelSession& ChannelSession = GetChannelSession(ChannelUri);
	ChannelSession.State = FChannelSession::EState::Connected;
	TriggerCompletionDelegate(ChannelSession.JoinDelegate, ChannelSession.ChannelName, FVoiceChatResult::CreateSuccess());

	OnVoiceChatChannelJoinedDelegate.Broadcast(ChannelSession.ChannelName);
}

void FVivoxVoiceChatUser::onInvalidChannelCredentials(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onInvalidChannelCredentials channel:%s"), ANSI_TO_TCHAR(ChannelUri.ToString()));

	FChannelSession& ChannelSession = GetChannelSession(ChannelUri);
	ChannelSession.State = FChannelSession::EState::Disconnected;
	TriggerCompletionDelegate(ChannelSession.JoinDelegate, ChannelSession.ChannelName, VoiceChat::Errors::CredentialsInvalid());

	RemoveChannelSession(ChannelSession.ChannelName);
}

void FVivoxVoiceChatUser::onChannelJoinFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onChannelJoinFailed channel:%s error:%s (%i)"), ANSI_TO_TCHAR(ChannelUri.ToString()), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());

	FChannelSession& ChannelSession = GetChannelSession(ChannelUri);
	ChannelSession.State = FChannelSession::EState::Disconnected;
	TriggerCompletionDelegate(ChannelSession.JoinDelegate, ChannelSession.ChannelName, ResultFromVivoxStatus(Status));

	RemoveChannelSession(ChannelSession.ChannelName);
}

void FVivoxVoiceChatUser::onChannelExited(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status)
{
	const FVoiceChatResult Result = ResultFromVivoxStatus(Status);
	if (Result.IsSuccess())
	{
		VIVOXVOICECHATUSER_LOG(Log, TEXT("onChannelExited ChannelUri:%s"), ANSI_TO_TCHAR(ChannelUri.ToString()));
	}
	else
	{
		VIVOXVOICECHATUSER_LOG(Warning, TEXT("onChannelExited ChannelUri:%s %s"), ANSI_TO_TCHAR(ChannelUri.ToString()), *LexToString(Result));
	}

	FChannelSession& ChannelSession = GetChannelSession(ChannelUri);
	const bool bWasConnecting = ChannelSession.State == FChannelSession::EState::Connecting;
	const bool bWasDisconnecting = ChannelSession.State == FChannelSession::EState::Disconnecting;
	ChannelSession.State = FChannelSession::EState::Disconnected;

	if (bWasConnecting)
	{
		// timeouts while connecting call onChannelExited instead of OnChannelJoinFailed
		TriggerCompletionDelegate(ChannelSession.JoinDelegate, ChannelSession.ChannelName, Result);
	}
	else
	{
		if (bWasDisconnecting)
		{
			TriggerCompletionDelegate(ChannelSession.LeaveDelegate, ChannelSession.ChannelName, Result);
		}

		OnVoiceChatChannelExitedDelegate.Broadcast(ChannelSession.ChannelName, Result);
	}

	RemoveChannelSession(ChannelSession.ChannelName);
}

void FVivoxVoiceChatUser::onCallStatsUpdated(const VivoxClientApi::AccountName& AccountName, vx_call_stats_t& Stats, bool bIsFinal)
{
	const int TotalPacketsLost = Stats.incoming_packetloss + Stats.incoming_discarded + Stats.incoming_out_of_time;
	const int TotalPackets = TotalPacketsLost + Stats.incoming_received;

	FVoiceChatCallStats CallStats;
	CallStats.CallLength = Stats.sample_interval_end - Stats.sample_interval_begin;
	CallStats.LatencyMinMeasuredSeconds = Stats.min_latency;
	CallStats.LatencyMaxMeasuredSeconds = Stats.max_latency;
	CallStats.LatencyAverageMeasuredSeconds = Stats.latency_measurement_count > 0 ? Stats.latency_sum / Stats.latency_measurement_count : 0.0;
	CallStats.PacketsNumLost = TotalPacketsLost;
	CallStats.PacketsNumTotal = TotalPackets;

	OnVoiceChatCallStatsUpdatedDelegate.Broadcast(CallStats);
}

void FVivoxVoiceChatUser::onParticipantAdded(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::Uri& ParticipantUri, bool bIsLoggedInUser)
{
	VIVOXVOICECHATUSER_LOG(Log, TEXT("onParticipantAdded channel:%s participant:%s"), ANSI_TO_TCHAR(ChannelUri.ToString()), ANSI_TO_TCHAR(ParticipantUri.ToString()));

	FChannelSession& ChannelSession = GetChannelSession(ChannelUri);
	FString PlayerName = VivoxVoiceChat.GetPlayerNameFromUri(ParticipantUri);
	FParticipant& ChannelParticipant = ChannelSession.Participants.Add(PlayerName);
	ChannelParticipant.PlayerName = PlayerName;
	ChannelParticipant.UserUri = ParticipantUri;

	OnVoiceChatPlayerAddedDelegate.Broadcast(ChannelSession.ChannelName, PlayerName);

	const FParticipant& Participant = GetParticipant(PlayerName);

	// Apply any existing mutes
	if (Participant.bMuted != ChannelParticipant.bMuted)
	{
		const bool bShouldMute = Participant.bMuted || Participant.Volume < SMALL_NUMBER;
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
		VivoxClientApi::VCSStatus Status = VivoxClientConnection.SetParticipantMutedForMe(LoginSession.AccountName, ChannelParticipant.UserUri, ChannelSession.ChannelUri, bShouldMute);
		if (Status.IsError())
		{
			VIVOXVOICECHATUSER_LOG(Warning, TEXT("SetParticipantMutedForMe failed: channel:%s user:%s muted:%s error:%s (%i)"), ANSI_TO_TCHAR(ChannelSession.ChannelUri.ToString()), ANSI_TO_TCHAR(ChannelParticipant.UserUri.ToString()), *LexToString(bShouldMute), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
			// TODO: This will fail only when Account/participant/channel is not found -> fixup our state
		}
	}

	// Apply any existing volume adjustments
	if (Participant.IntVolume != ChannelParticipant.IntVolume)
	{
		const bool bShouldMute = Participant.bMuted || Participant.Volume < SMALL_NUMBER;
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
		VivoxClientApi::VCSStatus Status = VivoxClientConnection.SetParticipantMutedForMe(LoginSession.AccountName, ChannelParticipant.UserUri, ChannelSession.ChannelUri, bShouldMute);
		if (Status.IsError())
		{
			VIVOXVOICECHATUSER_LOG(Warning, TEXT("SetParticipantMutedForMe failed: channel:%s user:%s muted:%s error:%s (%i)"), ANSI_TO_TCHAR(ChannelSession.ChannelUri.ToString()), ANSI_TO_TCHAR(ChannelParticipant.UserUri.ToString()), *LexToString(bShouldMute), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
			// TODO: This will fail only when Account/participant/channel is not found -> fixup our state
		}

		Status = VivoxClientConnection.SetParticipantAudioOutputDeviceVolumeForMe(LoginSession.AccountName, ChannelParticipant.UserUri, ChannelSession.ChannelUri, Participant.IntVolume);
		if (Status.IsError())
		{
			VIVOXVOICECHATUSER_LOG(Warning, TEXT("SetParticipantAudioOutputDeviceVolumeForMe failed: channel:%s user:%s volume:%i error:%s (%i)"), ANSI_TO_TCHAR(ChannelSession.ChannelUri.ToString()), ANSI_TO_TCHAR(ChannelParticipant.UserUri.ToString()), Participant.IntVolume, ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
			// TODO: This will fail only when Account/participant/channel is not found -> fixup our state
		}
	}
}

void FVivoxVoiceChatUser::onParticipantLeft(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::Uri& ParticipantUri, bool bIsLoggedInUser, VivoxClientApi::IClientApiEventHandler::ParticipantLeftReason Reason)
{
	VIVOXVOICECHATUSER_LOG(Log, TEXT("onParticipantLeft channel:%s participant:%s reason:%s"), ANSI_TO_TCHAR(ChannelUri.ToString()), ANSI_TO_TCHAR(ParticipantUri.ToString()), *LexToString(Reason));

	FChannelSession& ChannelSession = GetChannelSession(ChannelUri);
	FString PlayerName = VivoxVoiceChat.GetPlayerNameFromUri(ParticipantUri);

	ChannelSession.Participants.Remove(PlayerName);

	OnVoiceChatPlayerRemovedDelegate.Broadcast(ChannelSession.ChannelName, PlayerName);
}

void FVivoxVoiceChatUser::onParticipantUpdated(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::Uri& ParticipantUri, bool bIsLoggedInUser, bool bSpeaking, double MeterEnergy, bool bMutedForAll)
{
	VIVOXVOICECHATUSER_LOG(VeryVerbose, TEXT("onParticipantUpdated channel:%s participant:%s speaking:%s energy:%f"), ANSI_TO_TCHAR(ChannelUri.ToString()), ANSI_TO_TCHAR(ParticipantUri.ToString()), *LexToString(bSpeaking), MeterEnergy);

	FChannelSession& ChannelSession = GetChannelSession(ChannelUri);
	FString PlayerName = VivoxVoiceChat.GetPlayerNameFromUri(ParticipantUri);

	FParticipant& Participant = GetParticipant(PlayerName);
	Participant.bTalking = bSpeaking;

	if (FParticipant* ChannelParticipant = ChannelSession.Participants.Find(PlayerName))
	{
		ChannelParticipant->bTalking = bSpeaking;
		OnVoiceChatPlayerTalkingUpdatedDelegate.Broadcast(ChannelSession.ChannelName, ChannelParticipant->PlayerName, bSpeaking);
	}
}

void FVivoxVoiceChatUser::onAvailableAudioDevicesChanged()
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("onAvailableAudioDevicesChanged"));

	OnVoiceChatAvailableAudioDevicesChangedDelegate.Broadcast();
}

void FVivoxVoiceChatUser::onOperatingSystemChosenAudioInputDeviceChanged(const VivoxClientApi::AudioDeviceId& DeviceId)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("onOperatingSystemChosenAudioInputDeviceChanged deviceid:%s"), UTF8_TO_TCHAR(DeviceId.ToString()));
}

void FVivoxVoiceChatUser::onSetApplicationChosenAudioInputDeviceCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("onSetApplicationChosenAudioInputDeviceCompleted deviceid:%s"), UTF8_TO_TCHAR(DeviceId.ToString()));
}

void FVivoxVoiceChatUser::onSetApplicationChosenAudioInputDeviceFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId, const VivoxClientApi::VCSStatus& Status)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onSetApplicationChosenAudioInputDeviceFailed deviceid:%s error:%s (%i)"), UTF8_TO_TCHAR(DeviceId.ToString()), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
}

void FVivoxVoiceChatUser::onOperatingSystemChosenAudioOutputDeviceChanged(const VivoxClientApi::AudioDeviceId& DeviceId)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("onOperatingSystemChosenAudioOutputDeviceChanged deviceid:%s"), UTF8_TO_TCHAR(DeviceId.ToString()));
}

void FVivoxVoiceChatUser::onSetApplicationChosenAudioOutputDeviceCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("onSetApplicationChosenAudioOutputDeviceCompleted deviceid:%s"), UTF8_TO_TCHAR(DeviceId.ToString()));
}

void FVivoxVoiceChatUser::onSetApplicationChosenAudioOutputDeviceFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId, const VivoxClientApi::VCSStatus& Status)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onSetApplicationChosenAudioOutputDeviceFailed deviceid:%s error:%s (%i)"), UTF8_TO_TCHAR(DeviceId.ToString()), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
}

void FVivoxVoiceChatUser::onSetParticipantAudioOutputDeviceVolumeForMeCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& TargetUser, const VivoxClientApi::Uri& ChannelUri, int Volume)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("onSetParticipantAudioOutputDeviceVolumeForMeCompleted channel:%s user:%s volume:%i"), ANSI_TO_TCHAR(ChannelUri.ToString()), ANSI_TO_TCHAR(TargetUser.ToString()), Volume);

	FChannelSession& ChannelSession = GetChannelSession(ChannelUri);
	FString PlayerName = VivoxVoiceChat.GetPlayerNameFromUri(TargetUser);
	if (FParticipant* ChannelParticipant = ChannelSession.Participants.Find(PlayerName))
	{
		ChannelParticipant->Volume = static_cast<float>(Volume - VIVOX_MIN_VOL) / static_cast<float>(VIVOX_MAX_VOL - VIVOX_MIN_VOL);
		ChannelParticipant->IntVolume = Volume;
		OnVoiceChatPlayerVolumeUpdatedDelegate.Broadcast(ChannelSession.ChannelName, ChannelParticipant->PlayerName, ChannelParticipant->Volume);
	}
}

void FVivoxVoiceChatUser::onSetParticipantAudioOutputDeviceVolumeForMeFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& TargetUser, const VivoxClientApi::Uri& ChannelUri, int Volume, const VivoxClientApi::VCSStatus& Status)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onSetParticipantAudioOutputDeviceVolumeForMeFailed channel:%s user:%s volume:%i error:%s (%i)"), ANSI_TO_TCHAR(ChannelUri.ToString()), ANSI_TO_TCHAR(TargetUser.ToString()), Volume, ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());

	FChannelSession& ChannelSession = GetChannelSession(ChannelUri);
	FString PlayerName = VivoxVoiceChat.GetPlayerNameFromUri(TargetUser);
	if (FParticipant* ChannelParticipant = ChannelSession.Participants.Find(PlayerName))
	{
		// TODO: should this retry setting volume?
	}
}

void FVivoxVoiceChatUser::onSetChannelAudioOutputDeviceVolumeCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, int Volume)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("onSetChannelAudioOutputDeviceVolumeCompleted channel:%s volume:%i"), ANSI_TO_TCHAR(ChannelUri.ToString()), Volume);
}

void FVivoxVoiceChatUser::onSetChannelAudioOutputDeviceVolumeFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, int Volume, const VivoxClientApi::VCSStatus& Status)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onSetChannelAudioOutputDeviceVolumeFailed channel:%s volume:%i error:%s (%i)"), ANSI_TO_TCHAR(ChannelUri.ToString()), Volume, ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
}

void FVivoxVoiceChatUser::onSetParticipantMutedForMeCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& Target, const VivoxClientApi::Uri& ChannelUri, bool bMuted)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("onSetParticipantMutedForMeCompleted channel:%s user:%s muted:%s"), ANSI_TO_TCHAR(ChannelUri.ToString()), ANSI_TO_TCHAR(Target.ToString()), *LexToString(bMuted));

	FChannelSession& ChannelSession = GetChannelSession(ChannelUri);
	FString PlayerName = VivoxVoiceChat.GetPlayerNameFromUri(Target);
	if (FParticipant* ChannelParticipant = ChannelSession.Participants.Find(PlayerName))
	{
		// TODO: Determine how should this interact with mutes from setting volume to 0
		ChannelParticipant->bMuted = bMuted;
		OnVoiceChatPlayerMuteUpdatedDelegate.Broadcast(ChannelSession.ChannelName, ChannelParticipant->PlayerName, bMuted);
	}
}

void FVivoxVoiceChatUser::onSetParticipantMutedForMeFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& Target, const VivoxClientApi::Uri& ChannelUri, bool bMuted, const VivoxClientApi::VCSStatus& Status)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onSetParticipantMutedForMeFailed channel:%s user:%s muted:%s error:%s (%i)"), ANSI_TO_TCHAR(ChannelUri.ToString()), ANSI_TO_TCHAR(Target.ToString()), *LexToString(bMuted), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());

	FChannelSession& ChannelSession = GetChannelSession(ChannelUri);
	FString PlayerName = VivoxVoiceChat.GetPlayerNameFromUri(Target);
	if (FParticipant* ChannelParticipant = ChannelSession.Participants.Find(PlayerName))
	{
		// TODO: should this retry mute?
	}
}

void FVivoxVoiceChatUser::onSetChannelTransmissionToSpecificChannelCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("onSetChannelTransmissionToSpecificChannelCompleted channel:%s"), ANSI_TO_TCHAR(ChannelUri.ToString()));
}

void FVivoxVoiceChatUser::onSetChannelTransmissionToSpecificChannelFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onSetChannelTransmissionToSpecificChannelCompleted channel:%s error:%s (%i)"), ANSI_TO_TCHAR(ChannelUri.ToString()), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
}

void FVivoxVoiceChatUser::onSetChannelTransmissionToAllCompleted(const VivoxClientApi::AccountName& AccountName)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("onSetChannelTransmissionToAllCompleted"));
}

void FVivoxVoiceChatUser::onSetChannelTransmissionToAllFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onSetChannelTransmissionToAllFailed error:%s (%i)"), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
}

void FVivoxVoiceChatUser::onSetChannelTransmissionToNoneCompleted(const VivoxClientApi::AccountName& AccountName)
{
	VIVOXVOICECHATUSER_LOG(Verbose, TEXT("onSetChannelTransmissionToNoneCompleted"));
}

void FVivoxVoiceChatUser::onSetChannelTransmissionToNoneFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status)
{
	VIVOXVOICECHATUSER_LOG(Warning, TEXT("onSetChannelTransmissionToNoneFailed error:%s (%i)"), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
}

void FVivoxVoiceChatUser::onAudioUnitStarted(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri)
{

}

void FVivoxVoiceChatUser::onAudioUnitStopped(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri)
{

}

void FVivoxVoiceChatUser::onAudioUnitAfterCaptureAudioRead(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri, short* PcmFrames, int PcmFrameCount, int AudioFrameRate, int ChannelsPerFrame)
{
	if (SessionGroup != ANSI_TO_TCHAR(SessionGroupHandle))
	{
		return;
	}

	if (InitialTargetUri.IsValid())
	{
		FScopeLock Lock(&AfterCaptureAudioReadLock);
		OnVoiceChatAfterCaptureAudioReadDelegate.Broadcast(MakeArrayView(PcmFrames, PcmFrameCount * ChannelsPerFrame), AudioFrameRate, ChannelsPerFrame);
	}
	else
	{
		FScopeLock Lock(&AudioRecordLock);
		OnVoiceChatRecordSamplesAvailableDelegate.Broadcast(MakeArrayView(PcmFrames, PcmFrameCount * ChannelsPerFrame), AudioFrameRate, ChannelsPerFrame);
	}
}

void FVivoxVoiceChatUser::onAudioUnitBeforeCaptureAudioSent(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri, short* PcmFrames, int PcmFrameCount, int AudioFrameRate, int ChannelsPerFrame, bool bSpeaking)
{
	if (SessionGroup != ANSI_TO_TCHAR(SessionGroupHandle))
	{
		return;
	}

	if (InitialTargetUri.IsValid())
	{
		FScopeLock Lock(&BeforeCaptureAudioSentLock);
		OnVoiceChatBeforeCaptureAudioSentDelegate.Broadcast(MakeArrayView(PcmFrames, PcmFrameCount * ChannelsPerFrame), AudioFrameRate, ChannelsPerFrame, bSpeaking);
	}
}

void FVivoxVoiceChatUser::onAudioUnitBeforeRecvAudioRendered(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri, short* PcmFrames, int PcmFrameCount, int AudioFrameRate, int ChannelsPerFrame, bool bSilence)
{
	if (SessionGroup != ANSI_TO_TCHAR(SessionGroupHandle))
	{
		return;
	}

	if (InitialTargetUri.IsValid())
	{
		FScopeLock Lock(&BeforeRecvAudioRenderedLock);
		OnVoiceChatBeforeRecvAudioRenderedDelegate.Broadcast(MakeArrayView(PcmFrames, PcmFrameCount * ChannelsPerFrame), AudioFrameRate, ChannelsPerFrame, bSilence);
	}
}

static void* VivoxMalloc(size_t bytes)
{
	LLM_SCOPE(ELLMTag::RealTimeCommunications);
	return FMemory::Malloc(bytes);
}

static void VivoxFree(void* ptr)
{
	LLM_SCOPE(ELLMTag::RealTimeCommunications);
	FMemory::Free(ptr);
}

static void* VivoxRealloc(void* ptr, size_t bytes)
{
	LLM_SCOPE(ELLMTag::RealTimeCommunications);
	return FMemory::Realloc(ptr, bytes);
}

static void* VivoxCalloc(size_t num, size_t bytes)
{
	LLM_SCOPE(ELLMTag::RealTimeCommunications);
	const size_t Size = bytes * num;
	void* Ret = FMemory::Malloc(Size);
	FMemory::Memzero(Ret, Size);
	return Ret;
}

static void* VivoxMallocAligned(size_t alignment, size_t bytes)
{
	LLM_SCOPE(ELLMTag::RealTimeCommunications);
	return FMemory::Malloc(bytes, alignment);
}

static void VivoxFreeAligned(void* ptr)
{
	LLM_SCOPE(ELLMTag::RealTimeCommunications);
	FMemory::Free(ptr);
}

bool FVivoxVoiceChat::Initialize()
{
	if (!IsInitialized())
	{
		bool bEnabled = true;
		GConfig->GetBool(TEXT("VoiceChat.Vivox"), TEXT("bEnabled"), bEnabled, GEngineIni);
		if (bEnabled)
		{
			GConfig->GetString(TEXT("VoiceChat.Vivox"), TEXT("ServerUrl"), VivoxServerUrl, GEngineIni);
			GConfig->GetString(TEXT("VoiceChat.Vivox"), TEXT("Domain"), VivoxDomain, GEngineIni);
			GConfig->GetString(TEXT("VoiceChat.Vivox"), TEXT("Issuer"), VivoxIssuer, GEngineIni);
			GConfig->GetString(TEXT("VoiceChat.Vivox"), TEXT("Namespace"), VivoxNamespace, GEngineIni);
			GConfig->GetString(TEXT("VoiceChat.Vivox"), TEXT("InsecureSecret"), VivoxInsecureSecret, GEngineIni);

			if (VivoxNamespace.IsEmpty())
			{
				VivoxNamespace = VivoxIssuer;
			}

			// positional audio settings
			AttenuationModel = EVoiceChatAttenuationModel::InverseByDistance;
			MinDistance = 100;
			MaxDistance = 3000;
			Rolloff = 1.0f;
			FString AttenuationModelString;
			if (GConfig->GetString(TEXT("VoiceChat.Vivox"), TEXT("AttenuationModel"), AttenuationModelString, GEngineIni) && !AttenuationModelString.IsEmpty())
			{
				if (AttenuationModelString == TEXT("None"))
				{
					AttenuationModel = EVoiceChatAttenuationModel::None;
				}
				else if (AttenuationModelString == TEXT("InverseByDistance"))
				{
					AttenuationModel = EVoiceChatAttenuationModel::InverseByDistance;
				}
				else if (AttenuationModelString == TEXT("LinearByDistance"))
				{
					AttenuationModel = EVoiceChatAttenuationModel::LinearByDistance;
				}
				else if (AttenuationModelString == TEXT("ExponentialByDistance"))
				{
					AttenuationModel = EVoiceChatAttenuationModel::ExponentialByDistance;
				}
				else
				{
					UE_LOG(LogVivoxVoiceChat, Warning, TEXT("Unknown AttenuationModel: %s"), *AttenuationModelString);
				}
			}
			GConfig->GetInt(TEXT("VoiceChat.Vivox"), TEXT("MinDistance"), MinDistance, GEngineIni);
			GConfig->GetInt(TEXT("VoiceChat.Vivox"), TEXT("MaxDistance"), MaxDistance, GEngineIni);
			GConfig->GetFloat(TEXT("VoiceChat.Vivox"), TEXT("Rolloff"), Rolloff, GEngineIni);

			const char* VivoxVersionInfo = vx_get_sdk_version_info();
			UE_LOG(LogVivoxVoiceChat, Log, TEXT("Initializing Vivox %s"), ANSI_TO_TCHAR(VivoxVersionInfo));

			vx_sdk_config_t ConfigHints;
			int Result = vx_get_default_config3(&ConfigHints, sizeof(ConfigHints));
			if (Result != 0)
			{
				UE_LOG(LogVivoxVoiceChat, Warning, TEXT("Failed to get default config: error:%s (%i)"), ANSI_TO_TCHAR(VivoxClientApi::GetErrorString(Result)), Result);
			}
			else
			{
				SetVivoxSdkConfigHints(ConfigHints);

				LogLevel VivoxLogLevel = LogLevelWarning;
				FString LogLevelString;
				if (GConfig->GetString(TEXT("VoiceChat.Vivox"), TEXT("LogLevel"), LogLevelString, GEngineIni))
				{
					if (LogLevelString == TEXT("None"))
					{
						VivoxLogLevel = LogLevelNone;
					}
					else if (LogLevelString == TEXT("Error"))
					{
						VivoxLogLevel = LogLevelError;
					}
					else if (LogLevelString == TEXT("Warning"))
					{
						VivoxLogLevel = LogLevelWarning;
					}
					else if (LogLevelString == TEXT("Info"))
					{
						VivoxLogLevel = LogLevelInfo;
					}
					else if (LogLevelString == TEXT("Debug"))
					{
						VivoxLogLevel = LogLevelDebug;
					}
					else if (LogLevelString == TEXT("Trace"))
					{
						VivoxLogLevel = LogLevelTrace;
					}
				}

				CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
				VivoxClientApi::VCSStatus Status = VivoxClientConnection.Initialize(this, VivoxLogLevel, true, true, &ConfigHints, sizeof(ConfigHints));
				if (Status.IsError())
				{
					const FVoiceChatResult VoiceChatResult = ResultFromVivoxStatus(Status);
					UE_LOG(LogVivoxVoiceChat, Warning, TEXT("Initialize %s"), *LexToString(VoiceChatResult));
				}
				else
				{
					bInitialized = true;

					bool bVADAutomaticParameterSelection = true;
					GConfig->GetBool(TEXT("VoiceChat.Vivox"), TEXT("bVADAutomaticParameterSelection"), bVADAutomaticParameterSelection, GEngineIni);
					VivoxClientConnection.SetVADAutomaticParameterSelection(VivoxClientApi::AccountName(), bVADAutomaticParameterSelection);
				}
			}
		}
	}

	return IsInitialized();
}

void FVivoxVoiceChat::Initialize(const FOnVoiceChatInitializeCompleteDelegate& Delegate)
{
	const bool bResult = Initialize();
	const FVoiceChatResult Result = bResult ? FVoiceChatResult::CreateSuccess() : FVoiceChatResult(EVoiceChatResult::ImplementationError);
	if (Delegate.IsBound())
	{
		AsyncTask(ENamedThreads::GameThread, [Delegate, Result]()
		{
			Delegate.Execute(Result);
		});
	}
}

bool FVivoxVoiceChat::Uninitialize()
{
	if (IsInitialized())
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
		VivoxClientConnection.Uninitialize();
		ConnectionState = EConnectionState::Disconnected;
		DispatchAll(&FVivoxVoiceChatUser::onDisconnected, VivoxClientApi::Uri(TCHAR_TO_ANSI(*VivoxServerUrl)), VivoxClientApi::VCSStatus(VX_E_NOT_INITIALIZED));
		bInitialized = false;
	}

	return true;
}

void FVivoxVoiceChat::Uninitialize(const FOnVoiceChatUninitializeCompleteDelegate& Delegate)
{
	const bool bResult = Uninitialize();
	const FVoiceChatResult Result = bResult ? FVoiceChatResult::CreateSuccess() : FVoiceChatResult(EVoiceChatResult::ImplementationError);
	if (Delegate.IsBound())
	{
		AsyncTask(ENamedThreads::GameThread, [Delegate, Result]()
		{
			Delegate.Execute(Result);
		});
	}
}

bool FVivoxVoiceChat::IsInitialized() const
{
	return bInitialized;
}

IVoiceChatUser* FVivoxVoiceChat::CreateUser()
{
	FScopeLock Lock(&VoiceChatUsersCriticalSection);
	const TUniquePtr<FVivoxVoiceChatUser>& User = VoiceChatUsers.Emplace_GetRef(MakeUnique<FVivoxVoiceChatUser>(*this));
	return User.Get();
}

void FVivoxVoiceChat::ReleaseUser(IVoiceChatUser* User)
{
	if (User)
	{
		if (User == SingleUserVoiceChatUser)
		{
			UE_LOG(LogVivoxVoiceChat, Error, TEXT("ReleaseUser called in SingleUser mode"))
		}
		else
		{
			if (IsInitialized()
				&& IsConnected()
				&& User->IsLoggedIn())
			{
				User->Logout(FOnVoiceChatLogoutCompleteDelegate::CreateLambda([](const FString& PlayerName, const FVoiceChatResult& Result)
					{
						if (!Result.IsSuccess())
						{
							UE_LOG(LogVivoxVoiceChat, Warning, TEXT("UnregisterVoiceChatUser PlayerName=[%s] Logout %s"), *PlayerName, *LexToString(Result))
						}
					}));
			}

			FScopeLock Lock(&VoiceChatUsersCriticalSection);
			VoiceChatUsers.RemoveAll([User](TUniquePtr<FVivoxVoiceChatUser>& OtherUser)
			{
				if (User == OtherUser.Get())
				{
					OtherUser->bReleased = true;
					return true;
				}
				return false;
			});
		}
	}
}

void FVivoxVoiceChat::SetVivoxSdkConfigHints(vx_sdk_config_t& Hints)
{
	Hints.pf_malloc_func = &VivoxMalloc;
	Hints.pf_realloc_func = &VivoxRealloc;
	Hints.pf_calloc_func = &VivoxCalloc;
	Hints.pf_malloc_aligned_func = &VivoxMallocAligned;
	Hints.pf_free_func = &VivoxFree;
	Hints.pf_free_aligned_func = &VivoxFreeAligned;

	bool bEnableAudioDucking = false;
	GConfig->GetBool(TEXT("VoiceChat.Vivox"), TEXT("bEnableAudioDucking"), bEnableAudioDucking, GEngineIni);
	Hints.disable_audio_ducking = !bEnableAudioDucking;

	GConfig->GetInt(TEXT("VoiceChat.Vivox"), TEXT("RtpConnectTimeoutMs"), Hints.never_rtp_timeout_ms, GEngineIni);
	GConfig->GetInt(TEXT("VoiceChat.Vivox"), TEXT("RtpTimeoutMs"), Hints.lost_rtp_timeout_ms, GEngineIni);

	UE_LOG(LogVivoxVoiceChat, Verbose, TEXT("Rtp timeouts configured to: %ims %ims"), Hints.never_rtp_timeout_ms, Hints.lost_rtp_timeout_ms);
}

VivoxClientApi::AccountName FVivoxVoiceChat::CreateAccountName(const FString& PlayerName)
{
	// .Namespace.PlayerName.
	const FString AccountName  = FString::Printf(TEXT(".%s.%s."), *VivoxNamespace, *PlayerName);
	return VivoxClientApi::AccountName(TCHAR_TO_ANSI(*AccountName));
}

FString FVivoxVoiceChat::GetPlayerNameFromAccountName(const VivoxClientApi::AccountName& AccountName)
{
	FString AccountNameString = ANSI_TO_TCHAR(AccountName.ToString());
	// .Namespace.PlayerName.
	const int32 PrefixLength = 1 + VivoxNamespace.Len() + 1; // strlen(".") + VivoxNamespace.Len() + strlen(".")
	const int32 SuffixLength = 1; // strlen(".")
	if (PrefixLength + SuffixLength < AccountNameString.Len())
	{
		return AccountNameString.Mid(PrefixLength, AccountNameString.Len() - PrefixLength - SuffixLength);
	}
	else
	{
		return TEXT("INVALID");
	}
}

VivoxClientApi::Uri FVivoxVoiceChat::CreateUserUri(const FString& PlayerName)
{
	// sip:.Namespace.PlayerName.@Domain
	const FString UserUri = FString::Printf(TEXT("sip:.%s.%s.@%s"), *VivoxNamespace, *PlayerName, *VivoxDomain);
	return VivoxClientApi::Uri(TCHAR_TO_ANSI(*UserUri));
}

FString FVivoxVoiceChat::GetPlayerNameFromUri(const VivoxClientApi::Uri& UserUri)
{
	const FString UserUriString = ANSI_TO_TCHAR(UserUri.ToString());
	// sip:.Namespace.PlayerName.@Domain
	const int32 PrefixLength = 5 + VivoxNamespace.Len() + 1; // strlen("sip:.") + VivoxNamespace.Len() + strlen(".")
	const int32 SuffixLength = 2 + VivoxDomain.Len(); // strlen(".@") + VivoxDomain.Len()
	if (PrefixLength + SuffixLength < UserUriString.Len())
	{
		return UserUriString.Mid(PrefixLength, UserUriString.Len() - PrefixLength - SuffixLength);
	}
	else
	{
		return TEXT("INVALID");
	}
}

VivoxClientApi::Uri FVivoxVoiceChat::CreateChannelUri(const FString& ChannelName, EVoiceChatChannelType ChannelType, TOptional<FVoiceChatChannel3dProperties> Channel3dProperties)
{
	FString ChannelTypeString;
	switch (ChannelType)
	{
	case EVoiceChatChannelType::NonPositional:
		ChannelTypeString = TEXT("g");
		break;
	case EVoiceChatChannelType::Positional:
		ChannelTypeString = TEXT("d");
		break;
	case EVoiceChatChannelType::Echo:
		ChannelTypeString = TEXT("e");
		break;
	default:
		check(false);
		break;
	}

	FString Channel3dPropertiesString;
	if (ChannelType == EVoiceChatChannelType::Positional)
	{
		int AttenuationModelInt;
		switch (Channel3dProperties ? Channel3dProperties->AttenuationModel : AttenuationModel)
		{
		case EVoiceChatAttenuationModel::None:
			AttenuationModelInt = 0;
			break;
		default:
		case EVoiceChatAttenuationModel::InverseByDistance:
			AttenuationModelInt = 1;
			break;
		case EVoiceChatAttenuationModel::LinearByDistance:
			AttenuationModelInt = 2;
			break;
		case EVoiceChatAttenuationModel::ExponentialByDistance:
			AttenuationModelInt = 3;
			break;
		}

		// !MaxDistance-MinDistance-Rolloff-AttenuationModel
		if (Channel3dProperties)
		{
			Channel3dPropertiesString = FString::Printf(TEXT("!p-%i-%i-%.3f-%i"), static_cast<int>(Channel3dProperties->MaxDistance), static_cast<int>(Channel3dProperties->MinDistance), Channel3dProperties->Rolloff, AttenuationModelInt);
		}
		else
		{
			Channel3dPropertiesString = FString::Printf(TEXT("!p-%i-%i-%.3f-%i"), MaxDistance, MinDistance, Rolloff, AttenuationModelInt);
		}
	}

	// sip:confctl-?-Namespace.ChannelName[!3dProperties]@Domain
	const FString ChannelUri = FString::Printf(TEXT("sip:confctl-%s-%s.%s%s@%s"), *ChannelTypeString, *VivoxNamespace, *ChannelName, *Channel3dPropertiesString, *VivoxDomain);

	return VivoxClientApi::Uri(TCHAR_TO_ANSI(*ChannelUri));
}

FString FVivoxVoiceChat::GetChannelNameFromUri(const VivoxClientApi::Uri& ChannelUri)
{
	FString ChannelUriString = ANSI_TO_TCHAR(ChannelUri.ToString());
	// sip:confctl-?-Namespace.ChannelName@Domain
	const int32 PrefixLength = 14 + VivoxNamespace.Len() + 1; // strlen("sip:confctl-?-") + VivoxNamespace.Len() + strlen(".")
	const int32 SuffixLength = 1 + VivoxDomain.Len(); // strlen("@") + VivoxDomain.Len()

	if (PrefixLength + SuffixLength < ChannelUriString.Len())
	{
		FString Channel = ChannelUriString.Mid(PrefixLength, ChannelUriString.Len() - PrefixLength - SuffixLength);
		// strip off 3d properties
		int32 Channel3dParametersIndex = Channel.Find(TEXT("!p-"));
		if (Channel3dParametersIndex != INDEX_NONE)
		{
			Channel.LeftInline(Channel3dParametersIndex);
		}
		return Channel;
	}
	else
	{
		return TEXT("INVALID");
	}
}

EVoiceChatChannelType FVivoxVoiceChat::GetChannelTypeFromUri(const VivoxClientApi::Uri& ChannelUri)
{
	// sip:confctl-'ChannelType'-...
	switch (ChannelUri.ToString()[12])
	{
	default:
	case 'g':
		return EVoiceChatChannelType::NonPositional;
	case 'd':
		return EVoiceChatChannelType::Positional;
	case 'e':
		return EVoiceChatChannelType::Echo;
	}
}

FVivoxVoiceChatUser::FParticipant& FVivoxVoiceChatUser::GetParticipant(const FString& PlayerName)
{
	if (FParticipant* Participant = LoginSession.Participants.Find(PlayerName))
	{
		return *Participant;
	}

	FParticipant& NewParticipant = LoginSession.Participants.Add(PlayerName);
	NewParticipant.PlayerName = PlayerName;
	NewParticipant.UserUri = VivoxVoiceChat.CreateUserUri(PlayerName);
	return NewParticipant;
}

const FVivoxVoiceChatUser::FParticipant& FVivoxVoiceChatUser::GetParticipant(const FString& PlayerName) const
{
	if (const FParticipant* Participant = LoginSession.Participants.Find(PlayerName))
	{
		return *Participant;
	}
	else
	{
		static FParticipant NullParticipant;
		return NullParticipant;
	}
}

FVivoxVoiceChatUser::FChannelSession& FVivoxVoiceChatUser::GetChannelSession(const FString& ChannelName)
{
	if (FChannelSession* Session = LoginSession.ChannelSessions.Find(ChannelName))
	{
		return *Session;
	}

	FChannelSession& NewSession = LoginSession.ChannelSessions.Add(ChannelName);
	NewSession.ChannelName = ChannelName;
	NewSession.ChannelType = EVoiceChatChannelType::NonPositional;
	NewSession.ChannelUri = VivoxVoiceChat.CreateChannelUri(ChannelName, NewSession.ChannelType);
	return NewSession;
}

const FVivoxVoiceChatUser::FChannelSession& FVivoxVoiceChatUser::GetChannelSession(const FString& ChannelName) const
{
	if (const FChannelSession* Session = LoginSession.ChannelSessions.Find(ChannelName))
	{
		return *Session;
	}
	else
	{
		static FChannelSession NullSession;
		return NullSession;
	}
}

FVivoxVoiceChatUser::FChannelSession& FVivoxVoiceChatUser::GetChannelSession(const VivoxClientApi::Uri& ChannelUri)
{
	FString ChannelName = VivoxVoiceChat.GetChannelNameFromUri(ChannelUri);
	if (FChannelSession* Session = LoginSession.ChannelSessions.Find(ChannelName))
	{
		return *Session;
	}
	FChannelSession& Session = LoginSession.ChannelSessions.Add(ChannelName);
	Session.ChannelName = ChannelName;
	Session.ChannelType = VivoxVoiceChat.GetChannelTypeFromUri(ChannelUri);
	Session.ChannelUri = ChannelUri;
	return Session;
}

void FVivoxVoiceChatUser::RemoveChannelSession(const FString& ChannelName)
{
	// TODO: Should this trigger participant leave delegates?
	LoginSession.ChannelSessions.Remove(ChannelName);
}

void FVivoxVoiceChatUser::ClearChannelSessions()
{
	// TODO: Should this trigger channel/participant leave delegates?
	LoginSession.ChannelSessions.Reset();
}

void FVivoxVoiceChatUser::ClearLoginSession()
{
	ClearChannelSessions();
	LoginSession = FLoginSession();
}

void FVivoxVoiceChatUser::ApplyAudioInputOptions()
{
	if (LoginSession.AccountName.IsValid())
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
		VivoxClientConnection.SetMasterAudioInputDeviceVolume(LoginSession.AccountName, FMath::Lerp(VIVOX_MIN_VOL, VIVOX_MAX_VOL, AudioInputOptions.Volume));

		const bool bVolumeMuted = AudioInputOptions.Volume < SMALL_NUMBER;
		VivoxClientConnection.SetAudioInputDeviceMuted(LoginSession.AccountName, AudioInputOptions.bMuted || bVolumeMuted);
	}
}

void FVivoxVoiceChatUser::ApplyAudioInputDevicePolicy()
{
	if (LoginSession.AccountName.IsValid())
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
		if (AudioInputOptions.DevicePolicy.GetAudioDevicePolicy() == VivoxClientApi::AudioDevicePolicy::vx_audio_device_policy_specific_device)
		{
			VivoxClientConnection.SetApplicationChosenAudioInputDevice(LoginSession.AccountName, AudioInputOptions.DevicePolicy.GetSpecificAudioDevice());
		}
		else
		{
			VivoxClientConnection.UseOperatingSystemChosenAudioInputDevice(LoginSession.AccountName);
		}
	}
}

void FVivoxVoiceChatUser::ApplyAudioOutputOptions()
{
	if (LoginSession.AccountName.IsValid())
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
		VivoxClientConnection.SetMasterAudioOutputDeviceVolume(LoginSession.AccountName, FMath::Lerp(VIVOX_MIN_VOL, VIVOX_MAX_VOL, AudioOutputOptions.Volume));

		const bool bVolumeMuted = AudioOutputOptions.Volume < SMALL_NUMBER;
		VivoxClientConnection.SetAudioOutputDeviceMuted(LoginSession.AccountName, AudioOutputOptions.bMuted || bVolumeMuted);
	}
}

void FVivoxVoiceChatUser::ApplyAudioOutputDevicePolicy()
{
	if (LoginSession.AccountName.IsValid())
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
		if (AudioOutputOptions.DevicePolicy.GetAudioDevicePolicy() == VivoxClientApi::AudioDevicePolicy::vx_audio_device_policy_specific_device)
		{
			VivoxClientConnection.SetApplicationChosenAudioOutputDevice(LoginSession.AccountName, AudioOutputOptions.DevicePolicy.GetSpecificAudioDevice());
		}
		else
		{
			VivoxClientConnection.UseOperatingSystemChosenAudioOutputDevice(LoginSession.AccountName); 
		}
	}
}

bool FVivoxVoiceChat::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
#if NO_LOGGING
#define VIVOX_EXEC_LOG(...)
#else
#define VIVOX_EXEC_LOG(Fmt, ...) Ar.CategorizedLogf(LogVivoxVoiceChat.GetCategoryName(), ELogVerbosity::Log, Fmt, ##__VA_ARGS__)
#endif

	if (FParse::Command(&Cmd, TEXT("VIVOX")))
	{
		const TCHAR* SubCmd = Cmd;

		int RequestedUserIndex = -1;
		if (FParse::Value(Cmd, TEXT("INDEX="), RequestedUserIndex))
		{
			FParse::Token(Cmd, false); // skip over INDEX=#
		}

		if (FParse::Command(&Cmd, TEXT("LIST")))
		{
			for (int UserIndex = 0; UserIndex < VoiceChatUsers.Num(); ++UserIndex)
			{
				if (const TUniquePtr<FVivoxVoiceChatUser>& User = VoiceChatUsers[UserIndex])
				{
					VIVOX_EXEC_LOG(TEXT("VivoxUser Index:%i PlayerName:%s"), UserIndex, *User->GetLoggedInPlayerName());
				}
			}
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("INFO")))
		{
			VIVOX_EXEC_LOG(TEXT("Initialized: %s"), *LexToString(IsInitialized()));
			if (IsInitialized())
			{
				VIVOX_EXEC_LOG(TEXT("Connection Status: %s"), *ToString(ConnectionState));
				if (IsConnected())
				{
					VIVOX_EXEC_LOG(TEXT("  Server: %s"), *VivoxServerUrl);
					VIVOX_EXEC_LOG(TEXT("  Domain: %s"), *VivoxDomain);
				}
			}

			for (int UserIndex = 0; UserIndex < VoiceChatUsers.Num(); ++UserIndex)
			{
				if (const TUniquePtr<FVivoxVoiceChatUser>& User = VoiceChatUsers[UserIndex])
				{
					VIVOX_EXEC_LOG(TEXT("VivoxUser Index:%i PlayerName:%s"), UserIndex, *User->GetLoggedInPlayerName());
					User->Exec(InWorld, SubCmd, Ar);
				}
			}
			return true;
		}
#if !UE_BUILD_SHIPPING
		else if (FParse::Command(&Cmd, TEXT("INITIALIZE")))
		{
			Initialize();
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("UNINITIALIZE")))
		{
			Uninitialize();
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("CONNECT")))
		{
			Connect(FOnVoiceChatConnectCompleteDelegate::CreateLambda([](const FVoiceChatResult& Result)
			{
				UE_LOG(LogVivoxVoiceChat, Log, TEXT("VIVOX CONNECT result:%s"), *LexToString(Result));
			}));
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("DISCONNECT")))
		{
			Disconnect(FOnVoiceChatDisconnectCompleteDelegate::CreateLambda([](const FVoiceChatResult& Result)
			{
				UE_LOG(LogVivoxVoiceChat, Log, TEXT("VIVOX DISCONNECT result:%s"), *LexToString(Result));
			}));
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("CREATEUSER")))
		{
			(void)CreateUser();
			return true;
		}
		else if (RequestedUserIndex >= 0)
		{
			if (RequestedUserIndex < VoiceChatUsers.Num())
			{
				if (const TUniquePtr<FVivoxVoiceChatUser>& User = VoiceChatUsers[RequestedUserIndex])
				{
					if (FParse::Command(&Cmd, TEXT("DESTROYUSER")))
					{
						ReleaseUser(User.Get());
						return true;
					}
					else
					{
						return User->Exec(InWorld, Cmd, Ar);
					}
				}
			}
		}
		else if (SingleUserVoiceChatUser)
		{
			return SingleUserVoiceChatUser->Exec(InWorld, SubCmd, Ar);
		}
#endif // !UE_BUILD_SHIPPING
	}

#undef VIVOX_EXEC_LOG

	return false;
}

FString FVivoxVoiceChat::ToString(EConnectionState State)
{
	switch (State)
	{
	case EConnectionState::Disconnected:	return TEXT("Disconnected");
	case EConnectionState::Disconnecting:	return TEXT("Disconnecting");
	case EConnectionState::Connecting:		return TEXT("Connecting");
	case EConnectionState::Connected:		return TEXT("Connected");
	default:								return TEXT("Unknown");
	}
}

FString FVivoxVoiceChatUser::ToString(FLoginSession::EState State)
{
	switch (State)
	{
	case FLoginSession::EState::LoggedOut:	return TEXT("LoggedOut");
	case FLoginSession::EState::LoggingOut:	return TEXT("LoggingOut");
	case FLoginSession::EState::LoggingIn:	return TEXT("LoggingIn");
	case FLoginSession::EState::LoggedIn:	return TEXT("LoggedIn");
	default:								return TEXT("Unknown");
	}
}

FString FVivoxVoiceChatUser::ToString(FChannelSession::EState State)
{
	switch (State)
	{
	case FChannelSession::EState::Disconnected:	return TEXT("Disconnected");
	case FChannelSession::EState::Disconnecting:return TEXT("Disconnecting");
	case FChannelSession::EState::Connecting:	return TEXT("Connecting");
	case FChannelSession::EState::Connected:	return TEXT("Connected");
	default:									return TEXT("Unknown");
	}
}

FVivoxVoiceChat::FVivoxVoiceChat()
	: bInitialized(false)
	, ConnectionState(EConnectionState::Disconnected)
{
	SetClientConnection(&VivoxClientConnection);
	SetAbortEnabled(false);
}

FVivoxVoiceChat::~FVivoxVoiceChat()
{
	if (SingleUserVoiceChatUser)
	{
		SingleUserVoiceChatUser->bReleased = true;
	}
}

FVivoxVoiceChatUser& FVivoxVoiceChat::GetVoiceChatUser()
{
	if (!SingleUserVoiceChatUser)
	{
		SingleUserVoiceChatUser = static_cast<FVivoxVoiceChatUser*>(CreateUser());
		ensureMsgf(VoiceChatUsers.Num() == 1, TEXT("When using multiple users, all connections should be managed by an IVoiceChatUser"));
	}

	return *SingleUserVoiceChatUser;
}

FVivoxVoiceChatUser& FVivoxVoiceChat::GetVoiceChatUser() const
{
	return const_cast<FVivoxVoiceChat*>(this)->GetVoiceChatUser();
}

void FVivoxVoiceChat::SetSetting(const FString& Name, const FString& Value)
{
	GetVoiceChatUser().SetSetting(Name, Value);
}

FString FVivoxVoiceChat::GetSetting(const FString& Name)
{
	return GetVoiceChatUser().GetSetting(Name);
}

void FVivoxVoiceChat::SetAudioInputVolume(float Volume)
{
	GetVoiceChatUser().SetAudioInputVolume(Volume);
}

void FVivoxVoiceChat::SetAudioOutputVolume(float Volume)
{
	GetVoiceChatUser().SetAudioOutputVolume(Volume);
}

float FVivoxVoiceChat::GetAudioInputVolume() const
{
	return GetVoiceChatUser().GetAudioInputVolume();
}

float FVivoxVoiceChat::GetAudioOutputVolume() const
{
	return GetVoiceChatUser().GetAudioOutputVolume();
}

void FVivoxVoiceChat::SetAudioInputDeviceMuted(bool bIsMuted)
{
	GetVoiceChatUser().SetAudioInputDeviceMuted(bIsMuted);
}

void FVivoxVoiceChat::SetAudioOutputDeviceMuted(bool bIsMuted)
{
	GetVoiceChatUser().SetAudioOutputDeviceMuted(bIsMuted);
}

bool FVivoxVoiceChat::GetAudioInputDeviceMuted() const
{
	return GetVoiceChatUser().GetAudioInputDeviceMuted();
}

bool FVivoxVoiceChat::GetAudioOutputDeviceMuted() const
{
	return GetVoiceChatUser().GetAudioOutputDeviceMuted();
}

TArray<FVoiceChatDeviceInfo> FVivoxVoiceChat::GetAvailableInputDeviceInfos() const
{
	return GetVoiceChatUser().GetAvailableInputDeviceInfos();
}

TArray<FVoiceChatDeviceInfo> FVivoxVoiceChat::GetAvailableOutputDeviceInfos() const
{
	return GetVoiceChatUser().GetAvailableOutputDeviceInfos();
}

FOnVoiceChatAvailableAudioDevicesChangedDelegate& FVivoxVoiceChat::OnVoiceChatAvailableAudioDevicesChanged()
{
	return GetVoiceChatUser().OnVoiceChatAvailableAudioDevicesChanged();
}

void FVivoxVoiceChat::SetInputDeviceId(const FString& InputDeviceId)
{
	GetVoiceChatUser().SetInputDeviceId(InputDeviceId);
}

void FVivoxVoiceChat::SetOutputDeviceId(const FString& OutputDeviceId)
{
	GetVoiceChatUser().SetOutputDeviceId(OutputDeviceId);
}

FVoiceChatDeviceInfo FVivoxVoiceChat::GetInputDeviceInfo() const
{
	return GetVoiceChatUser().GetInputDeviceInfo();
}

FVoiceChatDeviceInfo FVivoxVoiceChat::GetOutputDeviceInfo() const
{
	return GetVoiceChatUser().GetOutputDeviceInfo();
}

FVoiceChatDeviceInfo FVivoxVoiceChat::GetDefaultInputDeviceInfo() const
{
	return GetVoiceChatUser().GetDefaultInputDeviceInfo();
}

FVoiceChatDeviceInfo FVivoxVoiceChat::GetDefaultOutputDeviceInfo() const
{
	return GetVoiceChatUser().GetDefaultOutputDeviceInfo();
}

void FVivoxVoiceChat::Connect(const FOnVoiceChatConnectCompleteDelegate& Delegate)
{
	FVoiceChatResult Result = FVoiceChatResult::CreateSuccess();

	if (!IsInitialized())
	{
		Result = VoiceChat::Errors::NotInitialized();
	}
	else if (ConnectionState == EConnectionState::Disconnecting)
	{
		Result = VoiceChat::Errors::DisconnectInProgress();
	}

	if (!Result.IsSuccess())
	{
		UE_LOG(LogVivoxVoiceChat, Warning, TEXT("Connect %s"), *LexToString(Result));

		Delegate.ExecuteIfBound(Result);
	}
	else if (IsConnected())
	{
		Delegate.ExecuteIfBound(FVoiceChatResult::CreateSuccess());
	}
	else
	{
		OnVoiceChatConnectCompleteDelegates.Add(Delegate);

		if (!IsConnecting())
		{
			if (VivoxServerUrl.IsEmpty() || VivoxDomain.IsEmpty() || VivoxNamespace.IsEmpty())
			{
				UE_LOG(LogVivoxVoiceChat, Warning, TEXT("[VoiceChat.Vivox] ServerUrl, Domain, or Issuer is not set. Vivox voice chat will not work"));
				Result = VoiceChat::Errors::MissingConfig();
				Result.ErrorDesc = FString::Printf(TEXT("ServerUrl=[%s] Domain=[%s] Namespace=[%s]"), *VivoxServerUrl, *VivoxDomain, *VivoxNamespace);
			}
			else
			{
				VivoxClientApi::Uri BackendUri(TCHAR_TO_ANSI(*VivoxServerUrl));
				CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
				VivoxClientApi::VCSStatus Status = VivoxClientConnection.Connect(BackendUri);

				if (Status.IsError())
				{
					UE_LOG(LogVivoxVoiceChat, Warning, TEXT("Connect failed: server:%s error:%s (%i)"), ANSI_TO_TCHAR(BackendUri.ToString()), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
					Result = ResultFromVivoxStatus(Status);
				}
				else
				{
					ConnectionState = EConnectionState::Connecting;
				}
			}

			if (!Result.IsSuccess())
			{
				ConnectionState = EConnectionState::Disconnected;

				TriggerCompletionDelegates(OnVoiceChatConnectCompleteDelegates, Result);
			}
		}
	}
}

void FVivoxVoiceChat::Disconnect(const FOnVoiceChatDisconnectCompleteDelegate& Delegate)
{
	if (IsConnected())
	{
		OnVoiceChatDisconnectCompleteDelegates.Add(Delegate);

		VivoxClientApi::Uri BackendUri(TCHAR_TO_ANSI(*VivoxServerUrl));
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
		VivoxClientConnection.Disconnect(BackendUri);

		ConnectionState = EConnectionState::Disconnecting;
	}
	else
	{
		Delegate.ExecuteIfBound(FVoiceChatResult::CreateSuccess());
	}
}

bool FVivoxVoiceChat::IsConnecting() const
{
	return ConnectionState == EConnectionState::Connecting;
}

bool FVivoxVoiceChat::IsConnected() const
{
	return ConnectionState == EConnectionState::Connected;
}

void FVivoxVoiceChat::Login(FPlatformUserId PlatformId, const FString& PlayerName, const FString& Credentials, const FOnVoiceChatLoginCompleteDelegate& Delegate)
{
	GetVoiceChatUser().Login(PlatformId, PlayerName, Credentials, Delegate);
}

void FVivoxVoiceChat::Logout(const FOnVoiceChatLogoutCompleteDelegate& Delegate)
{
	GetVoiceChatUser().Logout(Delegate);
}

bool FVivoxVoiceChat::IsLoggingIn() const
{
	return GetVoiceChatUser().IsLoggingIn();
}

bool FVivoxVoiceChat::IsLoggedIn() const
{
	return GetVoiceChatUser().IsLoggedIn();
}

FOnVoiceChatLoggedInDelegate& FVivoxVoiceChat::OnVoiceChatLoggedIn()
{
	return GetVoiceChatUser().OnVoiceChatLoggedIn();
}

FOnVoiceChatLoggedOutDelegate& FVivoxVoiceChat::OnVoiceChatLoggedOut()
{
	return GetVoiceChatUser().OnVoiceChatLoggedOut();
}

FString FVivoxVoiceChat::GetLoggedInPlayerName() const
{
	return GetVoiceChatUser().GetLoggedInPlayerName();
}

void FVivoxVoiceChat::BlockPlayers(const TArray<FString>& PlayerNames)
{
	GetVoiceChatUser().BlockPlayers(PlayerNames);
}

void FVivoxVoiceChat::UnblockPlayers(const TArray<FString>& PlayerNames)
{
	GetVoiceChatUser().UnblockPlayers(PlayerNames);
}

void FVivoxVoiceChat::JoinChannel(const FString& ChannelName, const FString& ChannelCredentials, EVoiceChatChannelType ChannelType, const FOnVoiceChatChannelJoinCompleteDelegate& Delegate, TOptional<FVoiceChatChannel3dProperties> Channel3dProperties)
{
	GetVoiceChatUser().JoinChannel(ChannelName, ChannelCredentials, ChannelType, Delegate, Channel3dProperties);
}

void FVivoxVoiceChat::LeaveChannel(const FString& Channel, const FOnVoiceChatChannelLeaveCompleteDelegate& Delegate)
{
	GetVoiceChatUser().LeaveChannel(Channel, Delegate);
}

FOnVoiceChatChannelJoinedDelegate& FVivoxVoiceChat::OnVoiceChatChannelJoined()
{
	return GetVoiceChatUser().OnVoiceChatChannelJoined();
}

FOnVoiceChatChannelExitedDelegate& FVivoxVoiceChat::OnVoiceChatChannelExited()
{
	return GetVoiceChatUser().OnVoiceChatChannelExited();
}

FOnVoiceChatCallStatsUpdatedDelegate& FVivoxVoiceChat::OnVoiceChatCallStatsUpdated()
{
	return GetVoiceChatUser().OnVoiceChatCallStatsUpdated();
}

void FVivoxVoiceChat::Set3DPosition(const FString& ChannelName, const FVector& SpeakerPosition, const FVector& ListenerPosition, const FVector& ListenerForwardDirection, const FVector& ListenerUpDirection)
{
	GetVoiceChatUser().Set3DPosition(ChannelName, SpeakerPosition, ListenerPosition, ListenerForwardDirection, ListenerUpDirection);
}

TArray<FString> FVivoxVoiceChat::GetChannels() const
{
	return GetVoiceChatUser().GetChannels();
}

TArray<FString> FVivoxVoiceChat::GetPlayersInChannel(const FString& ChannelName) const
{
	return GetVoiceChatUser().GetPlayersInChannel(ChannelName);
}

EVoiceChatChannelType FVivoxVoiceChat::GetChannelType(const FString& ChannelName) const
{
	return GetVoiceChatUser().GetChannelType(ChannelName);
}

FOnVoiceChatPlayerAddedDelegate& FVivoxVoiceChat::OnVoiceChatPlayerAdded()
{
	return GetVoiceChatUser().OnVoiceChatPlayerAdded();
}

FOnVoiceChatPlayerRemovedDelegate& FVivoxVoiceChat::OnVoiceChatPlayerRemoved()
{
	return GetVoiceChatUser().OnVoiceChatPlayerRemoved();
}

bool FVivoxVoiceChat::IsPlayerTalking(const FString& PlayerName) const
{
	return GetVoiceChatUser().IsPlayerTalking(PlayerName);
}

FOnVoiceChatPlayerTalkingUpdatedDelegate& FVivoxVoiceChat::OnVoiceChatPlayerTalkingUpdated()
{
	return GetVoiceChatUser().OnVoiceChatPlayerTalkingUpdated();
}

void FVivoxVoiceChat::SetPlayerMuted(const FString& PlayerName, bool bMuted)
{
	GetVoiceChatUser().SetPlayerMuted(PlayerName, bMuted);
}

bool FVivoxVoiceChat::IsPlayerMuted(const FString& PlayerName) const
{
	return GetVoiceChatUser().IsPlayerMuted(PlayerName);
}

FOnVoiceChatPlayerMuteUpdatedDelegate& FVivoxVoiceChat::OnVoiceChatPlayerMuteUpdated()
{
	return GetVoiceChatUser().OnVoiceChatPlayerMuteUpdated();
}

void FVivoxVoiceChat::SetPlayerVolume(const FString& PlayerName, float Volume)
{
	GetVoiceChatUser().SetPlayerVolume(PlayerName, Volume);
}

float FVivoxVoiceChat::GetPlayerVolume(const FString& PlayerName) const
{
	return GetVoiceChatUser().GetPlayerVolume(PlayerName);
}

FOnVoiceChatPlayerVolumeUpdatedDelegate& FVivoxVoiceChat::OnVoiceChatPlayerVolumeUpdated()
{
	return GetVoiceChatUser().OnVoiceChatPlayerVolumeUpdated();
}

void FVivoxVoiceChat::TransmitToAllChannels()
{
	GetVoiceChatUser().TransmitToAllChannels();
}

void FVivoxVoiceChat::TransmitToNoChannels()
{
	GetVoiceChatUser().TransmitToNoChannels();
}

void FVivoxVoiceChat::TransmitToSpecificChannel(const FString& ChannelName)
{
	GetVoiceChatUser().TransmitToSpecificChannel(ChannelName);
}

EVoiceChatTransmitMode FVivoxVoiceChat::GetTransmitMode() const
{
	return GetVoiceChatUser().GetTransmitMode();
}

FString FVivoxVoiceChat::GetTransmitChannel() const
{
	return GetVoiceChatUser().GetTransmitChannel();
}

FDelegateHandle FVivoxVoiceChat::StartRecording(const FOnVoiceChatRecordSamplesAvailableDelegate::FDelegate& Delegate)
{
	return GetVoiceChatUser().StartRecording(Delegate);
}

void FVivoxVoiceChat::StopRecording(FDelegateHandle Handle)
{
	GetVoiceChatUser().StopRecording(Handle);
}

FDelegateHandle FVivoxVoiceChat::RegisterOnVoiceChatAfterCaptureAudioReadDelegate(const FOnVoiceChatAfterCaptureAudioReadDelegate::FDelegate& Delegate)
{
	return GetVoiceChatUser().RegisterOnVoiceChatAfterCaptureAudioReadDelegate(Delegate);
}

void FVivoxVoiceChat::UnregisterOnVoiceChatAfterCaptureAudioReadDelegate(FDelegateHandle Handle)
{
	GetVoiceChatUser().UnregisterOnVoiceChatAfterCaptureAudioReadDelegate(Handle);
}

FDelegateHandle FVivoxVoiceChat::RegisterOnVoiceChatBeforeCaptureAudioSentDelegate(const FOnVoiceChatBeforeCaptureAudioSentDelegate::FDelegate& Delegate)
{
	return GetVoiceChatUser().RegisterOnVoiceChatBeforeCaptureAudioSentDelegate(Delegate);
}

void FVivoxVoiceChat::UnregisterOnVoiceChatBeforeCaptureAudioSentDelegate(FDelegateHandle Handle)
{
	GetVoiceChatUser().UnregisterOnVoiceChatBeforeCaptureAudioSentDelegate(Handle);
}

FDelegateHandle FVivoxVoiceChat::RegisterOnVoiceChatBeforeRecvAudioRenderedDelegate(const FOnVoiceChatBeforeRecvAudioRenderedDelegate::FDelegate& Delegate)
{
	return GetVoiceChatUser().RegisterOnVoiceChatBeforeRecvAudioRenderedDelegate(Delegate);
}

void FVivoxVoiceChat::UnregisterOnVoiceChatBeforeRecvAudioRenderedDelegate(FDelegateHandle Handle)
{
	GetVoiceChatUser().UnregisterOnVoiceChatBeforeRecvAudioRenderedDelegate(Handle);
}

FString FVivoxVoiceChat::InsecureGetLoginToken(const FString& PlayerName)
{
	return GetVoiceChatUser().InsecureGetLoginToken(PlayerName);
}

FString FVivoxVoiceChat::InsecureGetJoinToken(const FString& ChannelName, EVoiceChatChannelType ChannelType, TOptional<FVoiceChatChannel3dProperties> Channel3dProperties)
{
	return GetVoiceChatUser().InsecureGetJoinToken(ChannelName, ChannelType, Channel3dProperties);
}

template <typename TFn, typename... TArgs>
void FVivoxVoiceChat::DispatchUsingAccountName(const TFn& Fn, const VivoxClientApi::AccountName& AccountName, TArgs&&... Args)
{
	for (const TUniquePtr<FVivoxVoiceChatUser>& User : VoiceChatUsers)
	{
		if (User->LoginSession.AccountName.IsValid() && User->LoginSession.AccountName == AccountName)
		{
			(User.Get()->*Fn)(AccountName, Forward<TArgs>(Args)...);
		}
	}
}

template <typename TFn, typename... TArgs>
void FVivoxVoiceChat::DispatchAll(const TFn& Fn, TArgs&&... Args)
{
	for (const TUniquePtr<FVivoxVoiceChatUser>& User : VoiceChatUsers)
	{
		(User.Get()->*Fn)(Forward<TArgs>(Args)...);
	}
}

void FVivoxVoiceChat::InvokeOnUIThread(void (Func)(void* Arg0), void* Arg0)
{
	AsyncTask(ENamedThreads::GameThread, [Func, Arg0]()
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(VivoxVoiceChat)
		if (Func)
		{
			(*Func)(Arg0);
		}
	});

	FEmbeddedCommunication::WakeGameThread();
}

void FVivoxVoiceChat::onLogStatementEmitted(LogLevel Level, long long NativeMillisecondsSinceEpoch, long ThreadId, const char* LogMessageCStr)
{
	FString LogMessage = ANSI_TO_TCHAR(LogMessageCStr);
	bool bLogMatches = false;

	// Don't spam the log if we receive repeated log messages
	if (!LastLogMessage.IsEmpty())
	{
		bLogMatches = LogMessage == LastLogMessage && Level == LastLogLevel;
		if (bLogMatches)
		{
			LogSpamCount++;
		}

		const bool bLogSpamCountPowerOfTwo = FMath::IsPowerOfTwo(LogSpamCount);
		const bool bLogDueToPot = bLogMatches && bLogSpamCountPowerOfTwo;
		const bool bLogDueToChange = !bLogMatches && LogSpamCount > 0 && !bLogSpamCountPowerOfTwo; // Don't log on change if POT, as we already logged it.
		if (bLogDueToChange || bLogDueToPot)
		{
			if (LastLogLevel == LogLevelError)
			{
				UE_LOG(LogVivoxVoiceChat, Warning, TEXT("vivox: Error: %s (seen %d times)"), *LastLogMessage, LogSpamCount);
			}
			else
			{
				UE_LOG(LogVivoxVoiceChat, Log, TEXT("vivox: %s: %s (seen %d times)"), *GetLogLevelString(LastLogLevel), *LastLogMessage, LogSpamCount);
			}
		}
	}

	if (!bLogMatches)
	{
		if (Level == LogLevelError)
		{
			UE_LOG(LogVivoxVoiceChat, Warning, TEXT("vivox: Error: %s"), *LogMessage);
		}
		else
		{
			UE_LOG(LogVivoxVoiceChat, Log, TEXT("vivox: %s: %s"), *GetLogLevelString(Level), *LogMessage);
		}

		LogSpamCount = 0;
		LastLogMessage = MoveTemp(LogMessage);
		LastLogLevel = Level;
	}
}

void FVivoxVoiceChat::onConnectCompleted(const VivoxClientApi::Uri& Server)
{
	UE_LOG(LogVivoxVoiceChat, Log, TEXT("onConnectCompleted server:%s"), ANSI_TO_TCHAR(Server.ToString()));

	DispatchAll(&FVivoxVoiceChatUser::onConnectCompleted, Server);

	ConnectionState = EConnectionState::Connected;

	TriggerCompletionDelegates(OnVoiceChatConnectCompleteDelegates, FVoiceChatResult::CreateSuccess());
}

void FVivoxVoiceChat::onConnectFailed(const VivoxClientApi::Uri& Server, const VivoxClientApi::VCSStatus& Status)
{
	UE_LOG(LogVivoxVoiceChat, Warning, TEXT("onConnectFailed server:%s error:%s (%i)"), ANSI_TO_TCHAR(Server.ToString()), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());

	DispatchAll(&FVivoxVoiceChatUser::onConnectFailed, Server, Status);

	ConnectionState = EConnectionState::Disconnected;

	TriggerCompletionDelegates(OnVoiceChatConnectCompleteDelegates, ResultFromVivoxStatus(Status));
}

void FVivoxVoiceChat::onDisconnected(const VivoxClientApi::Uri& Server, const VivoxClientApi::VCSStatus& Status)
{
	if (Status.IsError())
	{
		UE_LOG(LogVivoxVoiceChat, Warning, TEXT("onDisconnected server:%s error:%s (%i)"), ANSI_TO_TCHAR(Server.ToString()), ANSI_TO_TCHAR(Status.ToString()), Status.GetStatusCode());
	}
	else
	{
		UE_LOG(LogVivoxVoiceChat, Log, TEXT("onDisconnected server:%s"), ANSI_TO_TCHAR(Server.ToString()));
	}

	DispatchAll(&FVivoxVoiceChatUser::onDisconnected, Server, Status);

	EConnectionState PreviousConnectionState = ConnectionState;
	ConnectionState = EConnectionState::Disconnected;

	if (PreviousConnectionState == EConnectionState::Disconnecting)
	{
		TriggerCompletionDelegates(OnVoiceChatDisconnectCompleteDelegates, FVoiceChatResult::CreateSuccess());
	}

	OnVoiceChatDisconnectedDelegate.Broadcast(ResultFromVivoxStatus(Status));
}

void FVivoxVoiceChat::onLoginCompleted(const VivoxClientApi::AccountName& AccountName)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onLoginCompleted, AccountName);
}

void FVivoxVoiceChat::onInvalidLoginCredentials(const VivoxClientApi::AccountName& AccountName)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onInvalidLoginCredentials, AccountName);
}

void FVivoxVoiceChat::onLoginFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onLoginFailed, AccountName, Status);
}

void FVivoxVoiceChat::onSessionGroupCreated(const VivoxClientApi::AccountName& AccountName, const char* SessionGroupHandle)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSessionGroupCreated, AccountName, SessionGroupHandle);
}

void FVivoxVoiceChat::onLogoutCompleted(const VivoxClientApi::AccountName& AccountName)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onLogoutCompleted, AccountName);
}

void FVivoxVoiceChat::onLogoutFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onLogoutFailed, AccountName, Status);
}

void FVivoxVoiceChat::onChannelJoined(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onChannelJoined, AccountName, ChannelUri);
}

void FVivoxVoiceChat::onInvalidChannelCredentials(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onInvalidChannelCredentials, AccountName, ChannelUri);
}

void FVivoxVoiceChat::onChannelJoinFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onChannelJoinFailed, AccountName, ChannelUri, Status);
}

void FVivoxVoiceChat::onChannelExited(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& ReasonCode)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onChannelExited, AccountName, ChannelUri, ReasonCode);
}

void FVivoxVoiceChat::onCallStatsUpdated(const VivoxClientApi::AccountName& AccountName, vx_call_stats_t& Stats, bool bIsFinal)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onCallStatsUpdated, AccountName, Stats, bIsFinal);
}

void FVivoxVoiceChat::onParticipantAdded(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::Uri& ParticipantUri, bool bIsLoggedInUser)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onParticipantAdded, AccountName, ChannelUri, ParticipantUri, bIsLoggedInUser);
}

void FVivoxVoiceChat::onParticipantLeft(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::Uri& ParticipantUri, bool bIsLoggedInUser, ParticipantLeftReason Reason)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onParticipantLeft, AccountName, ChannelUri, ParticipantUri, bIsLoggedInUser, Reason);
}

void FVivoxVoiceChat::onParticipantUpdated(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::Uri& ParticipantUri, bool bIsLoggedInUser, bool bSpeaking, double MeterEnergy, bool bMutedForAll)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onParticipantUpdated, AccountName, ChannelUri, ParticipantUri, bIsLoggedInUser, bSpeaking, MeterEnergy, bMutedForAll);
}

void FVivoxVoiceChat::onAvailableAudioDevicesChanged()
{
	DispatchAll(&FVivoxVoiceChatUser::onAvailableAudioDevicesChanged);
}
void FVivoxVoiceChat::onOperatingSystemChosenAudioInputDeviceChanged(const VivoxClientApi::AudioDeviceId& DeviceId)
{
	DispatchAll(&FVivoxVoiceChatUser::onOperatingSystemChosenAudioInputDeviceChanged, DeviceId);
}

void FVivoxVoiceChat::onSetApplicationChosenAudioInputDeviceCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetApplicationChosenAudioInputDeviceCompleted, AccountName, DeviceId);
}

void FVivoxVoiceChat::onSetApplicationChosenAudioInputDeviceFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId, const VivoxClientApi::VCSStatus& Status)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetApplicationChosenAudioInputDeviceFailed, AccountName, DeviceId, Status);
}

void FVivoxVoiceChat::onOperatingSystemChosenAudioOutputDeviceChanged(const VivoxClientApi::AudioDeviceId& DeviceId)
{
	DispatchAll(&FVivoxVoiceChatUser::onOperatingSystemChosenAudioOutputDeviceChanged, DeviceId);
}

void FVivoxVoiceChat::onSetApplicationChosenAudioOutputDeviceCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetApplicationChosenAudioOutputDeviceCompleted, AccountName, DeviceId);
}

void FVivoxVoiceChat::onSetApplicationChosenAudioOutputDeviceFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::AudioDeviceId& DeviceId, const VivoxClientApi::VCSStatus& Status)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetApplicationChosenAudioOutputDeviceFailed, AccountName, DeviceId, Status);
}

void FVivoxVoiceChat::onSetParticipantAudioOutputDeviceVolumeForMeCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& TargetUser, const VivoxClientApi::Uri& ChannelUri, int Volume)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetParticipantAudioOutputDeviceVolumeForMeCompleted, AccountName, TargetUser, ChannelUri, Volume);
}

void FVivoxVoiceChat::onSetParticipantAudioOutputDeviceVolumeForMeFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& TargetUser, const VivoxClientApi::Uri& ChannelUri, int Volume, const VivoxClientApi::VCSStatus& Status)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetParticipantAudioOutputDeviceVolumeForMeFailed, AccountName, TargetUser, ChannelUri, Volume, Status);
}

void FVivoxVoiceChat::onSetChannelAudioOutputDeviceVolumeCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, int Volume)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetChannelAudioOutputDeviceVolumeCompleted, AccountName, ChannelUri, Volume);
}

void FVivoxVoiceChat::onSetChannelAudioOutputDeviceVolumeFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, int Volume, const VivoxClientApi::VCSStatus& Status)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetChannelAudioOutputDeviceVolumeFailed, AccountName, ChannelUri, Volume, Status);
}

void FVivoxVoiceChat::onSetParticipantMutedForMeCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& Target, const VivoxClientApi::Uri& ChannelUri, bool bMuted)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetParticipantMutedForMeCompleted, AccountName, Target, ChannelUri, bMuted);
}

void FVivoxVoiceChat::onSetParticipantMutedForMeFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& Target, const VivoxClientApi::Uri& ChannelUri, bool bMuted, const VivoxClientApi::VCSStatus& Status)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetParticipantMutedForMeFailed, AccountName, Target, ChannelUri, bMuted, Status);
}

void FVivoxVoiceChat::onSetChannelTransmissionToSpecificChannelCompleted(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetChannelTransmissionToSpecificChannelCompleted, AccountName, ChannelUri);
}

void FVivoxVoiceChat::onSetChannelTransmissionToSpecificChannelFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::Uri& ChannelUri, const VivoxClientApi::VCSStatus& Status)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetChannelTransmissionToSpecificChannelFailed, AccountName, ChannelUri, Status);
}

void FVivoxVoiceChat::onSetChannelTransmissionToAllCompleted(const VivoxClientApi::AccountName& AccountName)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetChannelTransmissionToAllCompleted, AccountName);
}

void FVivoxVoiceChat::onSetChannelTransmissionToAllFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetChannelTransmissionToAllFailed, AccountName, Status);
}

void FVivoxVoiceChat::onSetChannelTransmissionToNoneCompleted(const VivoxClientApi::AccountName& AccountName)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetChannelTransmissionToNoneCompleted, AccountName);
}

void FVivoxVoiceChat::onSetChannelTransmissionToNoneFailed(const VivoxClientApi::AccountName& AccountName, const VivoxClientApi::VCSStatus& Status)
{
	DispatchUsingAccountName(&FVivoxVoiceChatUser::onSetChannelTransmissionToNoneFailed, AccountName, Status);
}

void FVivoxVoiceChat::onAudioUnitStarted(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri)
{
	FScopeLock Lock(&VoiceChatUsersCriticalSection);
	DispatchAll(&FVivoxVoiceChatUser::onAudioUnitStarted, SessionGroupHandle, InitialTargetUri);
}

void FVivoxVoiceChat::onAudioUnitStopped(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri)
{
	FScopeLock Lock(&VoiceChatUsersCriticalSection);
	DispatchAll(&FVivoxVoiceChatUser::onAudioUnitStopped, SessionGroupHandle, InitialTargetUri);
}

void FVivoxVoiceChat::onAudioUnitAfterCaptureAudioRead(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri, short* PcmFrames, int PcmFrameCount, int AudioFrameRate, int ChannelsPerFrame)
{
	FScopeLock Lock(&VoiceChatUsersCriticalSection);
	DispatchAll(&FVivoxVoiceChatUser::onAudioUnitAfterCaptureAudioRead, SessionGroupHandle, InitialTargetUri, PcmFrames, PcmFrameCount, AudioFrameRate, ChannelsPerFrame);
}

void FVivoxVoiceChat::onAudioUnitBeforeCaptureAudioSent(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri, short* PcmFrames, int PcmFrameCount, int AudioFrameRate, int ChannelsPerFrame, bool bSpeaking)
{
	FScopeLock Lock(&VoiceChatUsersCriticalSection);
	DispatchAll(&FVivoxVoiceChatUser::onAudioUnitBeforeCaptureAudioSent, SessionGroupHandle, InitialTargetUri, PcmFrames, PcmFrameCount, AudioFrameRate, ChannelsPerFrame, bSpeaking);
}

void FVivoxVoiceChat::onAudioUnitBeforeRecvAudioRendered(const char* SessionGroupHandle, const VivoxClientApi::Uri& InitialTargetUri, short* PcmFrames, int PcmFrameCount, int AudioFrameRate, int ChannelsPerFrame, bool bSilence)
{
	FScopeLock Lock(&VoiceChatUsersCriticalSection);
	DispatchAll(&FVivoxVoiceChatUser::onAudioUnitBeforeRecvAudioRendered, SessionGroupHandle, InitialTargetUri, PcmFrames, PcmFrameCount, AudioFrameRate, ChannelsPerFrame, bSilence);
}
